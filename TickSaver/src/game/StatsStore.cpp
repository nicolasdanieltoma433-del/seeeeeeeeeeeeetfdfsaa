#include "StatsStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>

#include <asp/fs.hpp>  // asp::fs::removeFile (Geode v6 usa asp::fs en vez de std::filesystem)

#include <cstdint>

using namespace geode::prelude;

namespace ts::store {
namespace {

// FNV-1a de 32 bits: estable entre plataformas y versiones (std::hash no lo es).
std::uint32_t fnv1a(std::string_view s) {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

asp::fs::Path levelsDir() { return Mod::get()->getSaveDir() / "levels"; }

asp::fs::Path levelPath(std::string const& key) { return levelsDir() / (key + ".json"); }

matjson::Value histogramToJson(Histogram const& h) {
    auto arr = matjson::Value::array();
    for (auto v : h) arr.push(matjson::Value(v));
    return arr;
}

void histogramFromJson(matjson::Value const& json, Histogram& out) {
    if (!json.isArray()) return;
    for (std::size_t i = 0; i < out.size() && i < json.size(); ++i) {
        out[i] = static_cast<std::uint32_t>(json[i].asUInt().unwrapOr(0));
    }
}

matjson::Value toJson(LevelStats const& s) {
    auto modes = matjson::Value::object();
    for (std::size_t i = 0; i < kModeCount; ++i) modes[kModeKeys[i]] = matjson::Value(s.savesByMode[i]);

    return matjson::makeObject({
        {"v", LevelStats::kSchemaVersion},
        {"key", s.key},
        {"name", s.name},
        {"attempts", s.attempts},
        {"deaths", s.deaths},
        {"clicks", s.clicks},
        {"saves", s.saves},
        {"unsalvageable", s.unsalvageable},
        {"bestStreak", s.bestStreak},
        {"savedTicksSum", s.savedTicksSum},
        {"maxSavedTicks", s.maxSavedTicks},
        {"bestPercent", static_cast<double>(s.bestPercent)},
        {"savesByPct", histogramToJson(s.savesByPct)},
        {"deathsByPct", histogramToJson(s.deathsByPct)},
        {"savesByMode", modes},
    });
}

LevelStats fromJson(matjson::Value const& j) {
    LevelStats s;
    auto u32 = [&](char const* k) { return static_cast<std::uint32_t>(j[k].asUInt().unwrapOr(0)); };
    s.key = j["key"].asString().unwrapOr("");
    s.name = j["name"].asString().unwrapOr("");
    s.attempts = u32("attempts");
    s.deaths = u32("deaths");
    s.clicks = u32("clicks");
    s.saves = u32("saves");
    s.unsalvageable = u32("unsalvageable");
    s.bestStreak = u32("bestStreak");
    s.savedTicksSum = j["savedTicksSum"].asUInt().unwrapOr(0);
    s.maxSavedTicks = u32("maxSavedTicks");
    s.bestPercent = static_cast<float>(j["bestPercent"].asDouble().unwrapOr(0.0));
    histogramFromJson(j["savesByPct"], s.savesByPct);
    histogramFromJson(j["deathsByPct"], s.deathsByPct);
    for (std::size_t i = 0; i < kModeCount; ++i) {
        s.savesByMode[i] = static_cast<std::uint32_t>(j["savesByMode"][kModeKeys[i]].asUInt().unwrapOr(0));
    }
    return s;
}

AdaptiveModel g_adaptive;

} // namespace

std::string levelKey(GJGameLevel* level) {
    if (!level) return "unknown";
    int id = level->m_levelID.value();
    if (id > 0) return fmt::format("id-{}", id);
    return fmt::format("local-{:08x}", fnv1a(level->m_levelName));
}

LevelStats loadLevel(GJGameLevel* level) {
    auto key = levelKey(level);
    LevelStats stats;
    if (auto json = file::readJson(levelPath(key))) {
        stats = fromJson(json.unwrap());
    }
    // Archivo inexistente o corrupto: empezamos de cero sin romper nada.
    stats.key = key;
    if (level) stats.name = level->m_levelName;
    return stats;
}

void saveLevel(LevelStats const& stats) {
    if (auto res = file::createDirectoryAll(levelsDir()); !res) {
        log::warn("TickSaver: no se pudo crear la carpeta de stats: {}", res.unwrapErr());
        return;
    }
    // writeStringSafe escribe a un temporal y renombra: si el juego se cierra
    // a mitad de escritura no queda un JSON a medias.
    if (auto res = file::writeStringSafe(levelPath(stats.key), toJson(stats).dump(matjson::NO_INDENTATION)); !res) {
        log::warn("TickSaver: no se pudieron guardar las stats de {}: {}", stats.key, res.unwrapErr());
    }
}

void deleteLevel(std::string const& key) { (void)asp::fs::removeFile(levelPath(key)); }

AdaptiveModel& adaptive() { return g_adaptive; }

void loadAdaptive() {
    auto json = Mod::get()->getSavedValue<matjson::Value>("adaptive");
    if (!json.isArray()) return;
    std::vector<double> flat;
    for (std::size_t i = 0; i < json.size(); ++i) flat.push_back(json[i].asDouble().unwrapOr(0.0));
    if (!g_adaptive.fromFlat(flat)) {
        log::info("TickSaver: modelo adaptativo con formato viejo, se reinicia");
        g_adaptive.clear();
    }
}

void saveAdaptive() {
    auto arr = matjson::Value::array();
    for (double v : g_adaptive.toFlat()) arr.push(matjson::Value(v));
    Mod::get()->setSavedValue<matjson::Value>("adaptive", arr);
}

} // namespace ts::store
