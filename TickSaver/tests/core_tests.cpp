// ============================================================================
//  TickSaver · tests/core_tests.cpp
//  Tests de la lógica pura (src/core). No necesitan Geode ni GD:
//
//    cmake -S tests -B build-tests && cmake --build build-tests && ./build-tests/core_tests
//
//  El "juego" se reemplaza por un MockPredictor donde decidimos a mano en
//  qué ticks apretar mata y en cuáles no.
// ============================================================================

#include "../src/core/AdaptiveModel.hpp"
#include "../src/core/Config.hpp"
#include "../src/core/ConfigCode.hpp"
#include "../src/core/InputScheduler.hpp"
#include "../src/core/Stats.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <variant>
#include <vector>

using namespace ts;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        ++g_checks;                                                                \
        if (!(cond)) {                                                             \
            ++g_failures;                                                          \
            std::printf("  FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                          \
    } while (0)

// ---------------------------------------------------------------------------
// Mock: "apretar en el tick `now` sobrevive si now >= firstViableTick".
// ---------------------------------------------------------------------------
struct MockPredictor final : IPredictor {
    Tick* clock = nullptr;
    std::function<SimOutcome(Slot, Tick, int)> press = [](Slot, Tick, int) { return SimOutcome::Survives; };
    std::function<SimOutcome(Slot, Tick)> idle = [](Slot, Tick) { return SimOutcome::Survives; };
    // Por defecto no apretar "muere" (= el click hace falta) y apretar tiene efecto.
    std::function<SimOutcome(Slot, Tick)> noPress = [](Slot, Tick) { return SimOutcome::Dies; };
    std::function<bool(Slot, Tick)> effect = [](Slot, Tick) { return true; };
    std::function<SimOutcome(Slot, Tick)> release = [](Slot, Tick) { return SimOutcome::Survives; };
    int lastHold = -1;

    SimOutcome simulatePress(Slot s, int hold) override {
        lastHold = hold;
        return press(s, *clock, hold);
    }
    SimOutcome simulateIdle(Slot s, int) override { return idle(s, *clock); }
    SimOutcome simulateNoPress(Slot s, int) override { return noPress(s, *clock); }
    bool pressHasEffect(Slot s, int) override { return effect(s, *clock); }
    SimOutcome simulateRelease(Slot s) override { return release(s, *clock); }
};

static SimOutcome viableFrom(Tick now, Tick first) { return now >= first ? SimOutcome::Survives : SimOutcome::Dies; }

static std::array<SlotParams, kSlotCount> params(int maxDelay, int hold = 8) {
    SlotParams p;
    p.maxDelay = maxDelay;
    p.expectedHold = hold;
    return {p, p};
}

static void testCleanClick() {
    std::puts("[scheduler] click justo pasa sin latencia");
    Tick now = 100;
    MockPredictor pred;
    pred.clock = &now;
    InputScheduler s;
    auto r = s.onPhysicalPress(Slot::P1, now, params(6)[0], pred);
    CHECK(r.forwardNow);
    CHECK(!s.isHolding(Slot::P1));
    auto ev = s.drainEvents();
    CHECK(ev.size() == 1 && ev[0].kind == ClickKind::Clean && ev[0].earlyTicks == 0);
}

static void testEarlyClickSaved() {
    std::puts("[scheduler] click 3t temprano se salva en +3");
    Tick now = 100;
    MockPredictor pred;
    pred.clock = &now;
    pred.press = [](Slot, Tick t, int) { return viableFrom(t, 103); };
    InputScheduler s;

    auto r = s.onPhysicalPress(Slot::P1, now, params(6)[0], pred);
    CHECK(!r.forwardNow);
    CHECK(s.isHolding(Slot::P1));

    std::vector<InjectCmd> out;
    int evaluations = 0;
    auto counting = pred.press;
    pred.press = [&](Slot sl, Tick t, int h) { ++evaluations; return counting(sl, t, h); };
    s.onTick(now, params(6), pred, out);  // mismo tick del press: no se reevalúa
    CHECK(out.empty() && evaluations == 0);
    for (now = 101; now <= 102; ++now) {
        s.onTick(now, params(6), pred, out);
        CHECK(out.empty());
    }
    now = 103;
    s.onTick(now, params(6), pred, out);
    CHECK(out.size() == 1 && out[0].push && out[0].slot == Slot::P1);
    auto ev = s.drainEvents();
    CHECK(ev.size() == 1 && ev[0].kind == ClickKind::Saved && ev[0].earlyTicks == 3);
    CHECK(ev[0].pressTick == 100 && ev[0].applyTick == 103);
    CHECK(!s.isHolding(Slot::P1));
}

static void testPreserveHold() {
    std::puts("[scheduler] release retenido conserva la duracion del hold");
    Tick now = 100;
    MockPredictor pred;
    pred.clock = &now;
    pred.press = [](Slot, Tick t, int) { return viableFrom(t, 104); };
    InputScheduler s;
    s.setPreserveHold(true);

    s.onPhysicalPress(Slot::P1, now, params(10)[0], pred);
    std::vector<InjectCmd> out;
    now = 101; s.onTick(now, params(10), pred, out);
    now = 102; s.onTick(now, params(10), pred, out);
    CHECK(!s.onPhysicalRelease(Slot::P1, now, params(10)[0], pred));  // soltó a los 2 ticks: queda retenido
    now = 103; s.onTick(now, params(10), pred, out);
    CHECK(pred.lastHold == 2);  // el fantasma ya simula el hold real de 2 ticks
    CHECK(out.empty());
    now = 104; s.onTick(now, params(10), pred, out);
    CHECK(out.size() == 1 && out[0].push);
    out.clear();
    now = 105; s.onTick(now, params(10), pred, out);
    CHECK(out.empty());
    now = 106; s.onTick(now, params(10), pred, out);  // 104 + hold 2
    CHECK(out.size() == 1 && !out[0].push);
}

static void testMaxDelayReached() {
    std::puts("[scheduler] sin tick viable: se suelta al llegar al maximo");
    Tick now = 50;
    MockPredictor pred;
    pred.clock = &now;
    pred.press = [](Slot, Tick, int) { return SimOutcome::Dies; };
    InputScheduler s;
    s.onPhysicalPress(Slot::P1, now, params(4)[0], pred);
    std::vector<InjectCmd> out;
    for (now = 51; now <= 53; ++now) s.onTick(now, params(4), pred, out);
    CHECK(out.empty());
    now = 54;
    s.onTick(now, params(4), pred, out);
    CHECK(out.size() == 1 && out[0].push);
    auto ev = s.drainEvents();
    CHECK(ev.size() == 1 && ev[0].kind == ClickKind::Unsalvageable && ev[0].earlyTicks == 4);
}

static void testIdleWouldKill() {
    std::puts("[scheduler] si esperar tambien mata, se aplica enseguida");
    Tick now = 10;
    MockPredictor pred;
    pred.clock = &now;
    pred.press = [](Slot, Tick, int) { return SimOutcome::Dies; };
    pred.idle = [](Slot, Tick t) { return t >= 12 ? SimOutcome::Dies : SimOutcome::Survives; };
    InputScheduler s;
    s.onPhysicalPress(Slot::P1, now, params(30)[0], pred);
    std::vector<InjectCmd> out;
    now = 11; s.onTick(now, params(30), pred, out);
    CHECK(out.empty());
    now = 12; s.onTick(now, params(30), pred, out);
    CHECK(out.size() == 1);
    auto ev = s.drainEvents();
    CHECK(ev.size() == 1 && ev[0].kind == ClickKind::Unsalvageable && ev[0].earlyTicks == 2);
}

static void testDisabledAndSkipped() {
    std::puts("[scheduler] maxDelay 0 o sin presupuesto: nunca retiene");
    Tick now = 0;
    MockPredictor pred;
    pred.clock = &now;
    pred.press = [](Slot, Tick, int) { return SimOutcome::Skipped; };
    InputScheduler s;
    CHECK(s.onPhysicalPress(Slot::P1, now, params(0)[0], pred).forwardNow);
    CHECK(s.onPhysicalPress(Slot::P1, now, params(10)[0], pred).forwardNow);
    auto ev = s.drainEvents();
    CHECK(ev.size() == 2 && ev[0].kind == ClickKind::Unchecked && ev[1].kind == ClickKind::Unchecked);
}

static void testDisableWhileHolding() {
    std::puts("[scheduler] desactivar el mod con un click retenido lo suelta sin juzgarlo");
    Tick now = 0;
    MockPredictor pred;
    pred.clock = &now;
    pred.press = [](Slot, Tick, int) { return SimOutcome::Dies; };
    InputScheduler s;
    s.onPhysicalPress(Slot::P1, now, params(10)[0], pred);
    CHECK(s.isHolding(Slot::P1));
    std::vector<InjectCmd> out;
    now = 1;
    s.onTick(now, params(0), pred, out);  // maxDelay 0 = mod apagado
    CHECK(out.size() == 1 && out[0].push);
    auto ev = s.drainEvents();
    CHECK(ev.size() == 1 && ev[0].kind == ClickKind::Unchecked);
}

static void testRetapDropsOldTap() {
    std::puts("[scheduler] un tap nuevo reemplaza al tap retenido (no se funden en un hold)");
    Tick now = 0;
    MockPredictor pred;
    pred.clock = &now;
    pred.press = [](Slot, Tick, int) { return SimOutcome::Dies; };
    InputScheduler s;
    s.onPhysicalPress(Slot::P1, now, params(30)[0], pred);
    s.onPhysicalRelease(Slot::P1, 1, params(30)[0], pred);
    CHECK(s.isHolding(Slot::P1));
    now = 3;
    pred.press = [](Slot, Tick, int) { return SimOutcome::Survives; };
    auto r = s.onPhysicalPress(Slot::P1, now, params(30)[0], pred);
    CHECK(r.forwardNow);           // el tap nuevo es bueno: pasa en el acto
    CHECK(!s.isHolding(Slot::P1)); // y el viejo se descartó
    std::vector<InjectCmd> out;
    for (now = 4; now < 40; ++now) s.onTick(now, params(30), pred, out);
    CHECK(out.empty());            // nunca se reinyecta el tap descartado
}

static void testTwoSlotsIndependent() {
    std::puts("[scheduler] 2 jugadores: cada slot se corrige por separado");
    Tick now = 0;
    MockPredictor pred;
    pred.clock = &now;
    pred.press = [](Slot slot, Tick t, int) { return slot == Slot::P1 ? viableFrom(t, 2) : viableFrom(t, 5); };
    InputScheduler s;
    s.onPhysicalPress(Slot::P1, now, params(10)[0], pred);
    s.onPhysicalPress(Slot::P2, now, params(10)[1], pred);
    std::vector<InjectCmd> out;
    std::vector<std::pair<Tick, InjectCmd>> log;
    for (now = 1; now <= 6; ++now) {
        out.clear();
        s.onTick(now, params(10), pred, out);
        for (auto c : out) log.push_back({now, c});
    }
    CHECK(log.size() == 2);
    CHECK(log[0].first == 2 && log[0].second.slot == Slot::P1);
    CHECK(log[1].first == 5 && log[1].second.slot == Slot::P2);
}

static void testRepressFlushesRelease() {
    std::puts("[scheduler] volver a apretar libera el release programado");
    Tick now = 0;
    MockPredictor pred;
    pred.clock = &now;
    pred.press = [](Slot, Tick t, int) { return viableFrom(t, 2); };
    InputScheduler s;
    s.onPhysicalPress(Slot::P1, now, params(10)[0], pred);
    std::vector<InjectCmd> out;
    now = 1;
    s.onPhysicalRelease(Slot::P1, now, params(10)[0], pred);  // hold de 1 tick
    s.onTick(now, params(10), pred, out);
    now = 2; s.onTick(now, params(10), pred, out);  // press inyectado, release programado en 3
    CHECK(out.size() == 1 && out[0].push);
    pred.press = [](Slot, Tick, int) { return SimOutcome::Survives; };
    auto r = s.onPhysicalPress(Slot::P1, now, params(10)[0], pred);  // re-press en el mismo tick
    CHECK(r.releaseFirst && r.forwardNow);
}

// ---------------------------------------------------------------------------

static void testConfigCodeRoundtrip() {
    std::puts("[config] TS2 ida y vuelta");
    Config cfg;
    applyPreset(cfg, Preset::Wave);
    cfg.region = Region{true, 40, 60, 12};
    cfg.adaptive = true;
    cfg.hudScale = 1.2f;
    auto code = encodeConfig(cfg);
    std::printf("  codigo sin paleta (%zu chars): %s\n", code.size(), code.c_str());
    CHECK(code.rfind("TS2-", 0) == 0);

    auto res = decodeConfig(code, Config{});
    CHECK(std::holds_alternative<Config>(res));
    if (auto* out = std::get_if<Config>(&res)) {
        CHECK(out->globalMaxTicks == cfg.globalMaxTicks);
        CHECK(out->modeMaxTicks == cfg.modeMaxTicks);
        CHECK(out->region == cfg.region);
        CHECK(out->adaptive);
        CHECK(out->palette == Config::defaultPalette());
        CHECK(out->hudScale > 1.19f && out->hudScale < 1.21f);
        CHECK(out->preset == Preset::Custom);  // adaptive=true no es parte de "Wave"
    }

    cfg.palette[0] = RGBA{1, 2, 3, 4};
    cfg.palette[7] = RGBA{250, 251, 252, 253};
    auto code2 = encodeConfig(cfg);
    std::printf("  codigo con paleta (%zu chars)\n", code2.size());
    auto res2 = decodeConfig(code2, Config{});
    CHECK(std::holds_alternative<Config>(res2));
    if (auto* out = std::get_if<Config>(&res2)) CHECK(out->palette == cfg.palette);

    // Minúsculas, sin guiones y con O/0 confundidos sigue funcionando.
    std::string sloppy;
    for (char c : code) {
        if (c == '-') continue;
        sloppy += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    sloppy.insert(0, "TS2-");
    sloppy.erase(4, 3);  // quitamos el "ts2" duplicado que quedó tras el prefijo
    CHECK(std::holds_alternative<Config>(decodeConfig(sloppy, Config{})));
}

static void testConfigCodeErrors() {
    std::puts("[config] TS2 rechaza codigos invalidos");
    Config cfg;
    auto code = encodeConfig(cfg);
    CHECK(std::holds_alternative<CodeError>(decodeConfig("hola", cfg)));
    CHECK(std::holds_alternative<CodeError>(decodeConfig("TS2-", cfg)));
    CHECK(std::holds_alternative<CodeError>(decodeConfig("TS2-#####", cfg)));
    // Cambiar un caracter rompe el checksum.
    std::string bad = code;
    bad[6] = bad[6] == 'A' ? 'B' : 'A';
    CHECK(std::holds_alternative<CodeError>(decodeConfig(bad, cfg)));
}

static void testPresetsAndRegion() {
    std::puts("[config] presets, overrides por modo y tramo extra");
    Config cfg;
    applyPreset(cfg, Preset::Normal);
    CHECK(detectPreset(cfg) == Preset::Normal);
    CHECK(cfg.maxTicksFor(Mode::Cube, 10.f) == 6);
    CHECK(cfg.maxTicksFor(Mode::Wave, 10.f) == 5);   // override de modo
    CHECK(cfg.maxTicksFor(Mode::Swing, 10.f) == 4);  // swing con sus propios ticks
    cfg.region = Region{true, 40, 60, 10};
    CHECK(cfg.maxTicksFor(Mode::Cube, 50.f) == 16);
    CHECK(cfg.maxTicksFor(Mode::Cube, 61.f) == 6);
    applyPreset(cfg, Preset::Max);
    CHECK(cfg.maxTicksFor(Mode::Cube, 50.f) == 255);  // tope duro
    cfg.globalMaxTicks = 255;
    cfg.region.extraTicks = 255;
    CHECK(cfg.maxTicksFor(Mode::Cube, 50.f) == 255);  // base + tramo no pasa de 255
    cfg.globalMaxTicks = 7;
    CHECK(detectPreset(cfg) == Preset::Custom);
    CHECK(presetFromName("Entrenar") == Preset::Train);
    CHECK(!presetFromName("nope").has_value());
}

static void testAdaptive() {
    std::puts("[adaptativo] aprende tu earliness y tu hold");
    AdaptiveModel m;
    CHECK(m.adaptiveMax(Mode::Cube, 30) == 30);  // sin datos: no cambia
    for (int i = 0; i < 20; ++i) m.recordSave(Mode::Cube, 3);
    int learned = m.adaptiveMax(Mode::Cube, 30);
    std::printf("  max aprendido con saves de 3t: %d\n", learned);
    CHECK(learned >= 3 && learned <= 6);
    CHECK(m.adaptiveMax(Mode::Cube, 2) == 2);  // nunca supera lo configurado
    CHECK(m.expectedHold(Mode::Ship, 14) == 14);
    for (int i = 0; i < 20; ++i) m.recordHold(Mode::Ship, 20);
    CHECK(m.expectedHold(Mode::Ship, 14) == 20);

    AdaptiveModel copy;
    CHECK(copy.fromFlat(m.toFlat()));
    CHECK(copy.adaptiveMax(Mode::Cube, 30) == learned);
}

static void testStats() {
    std::puts("[stats] racha, resumen e histograma");
    Session s;
    s.startAttempt();
    ClickEvent clean{Slot::P1, ClickKind::Clean, 0, Mode::Cube, 0, 0};
    ClickEvent saved{Slot::P1, ClickKind::Saved, 3, Mode::Cube, 0, 3};
    s.onClick(clean);
    s.onClick(clean);
    CHECK(s.streak() == 2);
    s.onClick(saved);
    CHECK(s.streak() == 0 && s.bestStreakThisAttempt() == 2 && s.assisted());
    CHECK(describeClick(s.lastClick()) == "temprano por 3t: salvado");

    LevelStats ls;
    ls.recordClick(saved, 42.7f);
    ls.recordClick(saved, 44.1f);
    ls.recordClick(saved, 99.9f);
    ls.recordDeath(150.f);  // fuera de rango -> última celda
    CHECK(ls.saves == 3 && ls.savesByPct[42] == 1 && ls.savesByPct[99] == 1);
    CHECK(ls.deathsByPct[99] == 1);
    auto b = LevelStats::bucketed(ls.savesByPct, 20);
    CHECK(b.size() == 20 && b[8] == 2 && b[19] == 1);
    CHECK(ls.averageSavedTicks() == 3.0);
}

static SlotParams tapParams(int maxDelay) {
    SlotParams p;
    p.maxDelay = maxDelay;
    p.dropUnneeded = true;
    return p;
}

static void testUnnecessaryClickNotCounted() {
    std::puts("[scheduler] click sin efecto (cubo en el aire) pasa pero no cuenta");
    Tick now = 0;
    MockPredictor pred;
    pred.clock = &now;
    pred.effect = [](Slot, Tick) { return false; };
    InputScheduler s;
    CHECK(s.onPhysicalPress(Slot::P1, now, tapParams(10), pred).forwardNow);
    auto ev = s.drainEvents();
    CHECK(ev.size() == 1 && ev[0].kind == ClickKind::Unnecessary);
    Session session;
    session.startAttempt();
    session.onClick(ev[0]);
    CHECK(session.streak() == 0 && !session.lastClick().valid);
    LevelStats ls;
    ls.recordClick(ev[0], 10.f);
    CHECK(ls.clicks == 0);
}

static void testDeadlyUnneededClickDropped() {
    std::puts("[scheduler] click que mata y no hace falta: se descarta, no se dispara tarde");
    Tick now = 0;
    MockPredictor pred;
    pred.clock = &now;
    pred.press = [](Slot, Tick t, int) { return viableFrom(t, 5); };  // pinchos en el techo hasta t=5
    pred.noPress = [](Slot, Tick) { return SimOutcome::Survives; };    // en el piso no pasa nada
    InputScheduler s;
    CHECK(!s.onPhysicalPress(Slot::P1, now, tapParams(100), pred).forwardNow);
    std::vector<InjectCmd> out;
    for (now = 1; now <= 20; ++now) s.onTick(now, {tapParams(100), tapParams(100)}, pred, out);
    CHECK(out.empty());  // nunca se inyecta
    auto ev = s.drainEvents();
    CHECK(ev.size() == 1 && ev[0].kind == ClickKind::Ignored && ev[0].earlyTicks == 5);
    CHECK(!s.isHolding(Slot::P1));
}

static void testNeededEarlyClickStillApplied() {
    std::puts("[scheduler] click temprano que SI hace falta: se aplica (no se descarta)");
    Tick now = 0;
    MockPredictor pred;
    pred.clock = &now;
    pred.press = [](Slot, Tick t, int) { return viableFrom(t, 3); };
    pred.noPress = [](Slot, Tick) { return SimOutcome::Dies; };  // sin saltar te comés el pincho
    InputScheduler s;
    s.onPhysicalPress(Slot::P1, now, tapParams(100), pred);
    std::vector<InjectCmd> out;
    for (now = 1; now <= 3; ++now) s.onTick(now, {tapParams(100), tapParams(100)}, pred, out);
    CHECK(out.size() == 1 && out[0].push);
    auto ev = s.drainEvents();
    CHECK(ev.size() == 1 && ev[0].kind == ClickKind::Saved && ev[0].earlyTicks == 3);
}

static void testDropAtMaxDelay() {
    std::puts("[scheduler] al llegar al maximo, un click innecesario se descarta en vez de matarte");
    Tick now = 0;
    MockPredictor pred;
    pred.clock = &now;
    pred.press = [](Slot, Tick, int) { return SimOutcome::Dies; };
    pred.noPress = [](Slot, Tick) { return SimOutcome::Survives; };
    InputScheduler s;
    s.onPhysicalPress(Slot::P1, now, tapParams(4), pred);
    std::vector<InjectCmd> out;
    for (now = 1; now <= 4; ++now) s.onTick(now, {tapParams(4), tapParams(4)}, pred, out);
    CHECK(out.empty());
    auto ev = s.drainEvents();
    CHECK(ev.size() == 1 && ev[0].kind == ClickKind::Ignored);
}

static SlotParams holdParams(int maxDelay) {
    SlotParams p;
    p.maxDelay = maxDelay;
    p.saveReleases = true;
    p.mode = Mode::Wave;
    return p;
}

static void testEarlyReleaseSaved() {
    std::puts("[scheduler] wave: soltar temprano se retiene hasta que soltar sea seguro");
    Tick now = 10;
    MockPredictor pred;
    pred.clock = &now;
    pred.release = [](Slot, Tick t) { return viableFrom(t, 14); };
    InputScheduler s;
    CHECK(!s.onPhysicalRelease(Slot::P1, now, holdParams(20), pred));  // soltar en 10 mataba
    CHECK(s.isHolding(Slot::P1));
    std::vector<InjectCmd> out;
    for (now = 11; now <= 13; ++now) s.onTick(now, {holdParams(20), holdParams(20)}, pred, out);
    CHECK(out.empty());
    now = 14;
    s.onTick(now, {holdParams(20), holdParams(20)}, pred, out);
    CHECK(out.size() == 1 && !out[0].push);
    auto ev = s.drainEvents();
    CHECK(ev.size() == 1 && ev[0].kind == ClickKind::Saved && ev[0].release && ev[0].earlyTicks == 4);
    Session session;
    session.startAttempt();
    session.onClick(ev[0]);
    CHECK(describeClick(session.lastClick()) == "soltaste temprano por 4t: salvado");
}

static void testRepressCancelsHeldRelease() {
    std::puts("[scheduler] wave: volver a apretar con un soltar retenido lo cancela");
    Tick now = 0;
    MockPredictor pred;
    pred.clock = &now;
    pred.release = [](Slot, Tick) { return SimOutcome::Dies; };
    InputScheduler s;
    s.onPhysicalRelease(Slot::P1, now, holdParams(20), pred);
    now = 2;
    auto r = s.onPhysicalPress(Slot::P1, now, holdParams(20), pred);
    CHECK(!r.forwardNow);  // el juego ya lo tiene apretado
    CHECK(!s.isHolding(Slot::P1));
    std::vector<InjectCmd> out;
    for (now = 3; now < 30; ++now) s.onTick(now, {holdParams(20), holdParams(20)}, pred, out);
    CHECK(out.empty());
}

static void testConfigCodeV2StillImports() {
    std::puts("[config] codigos TS2 viejos (v2, ticks de 1 byte) se siguen importando");
    auto res = decodeConfig("TS2-08ZG8-1G01G-00060-00062-GF0C0-WYCP", Config{});
    CHECK(std::holds_alternative<Config>(res));
    if (auto* c = std::get_if<Config>(&res)) {
        CHECK(c->globalMaxTicks == 6);
        CHECK(c->modeMaxTicks[idx(Mode::Wave)] == 24 && c->modeMaxTicks[idx(Mode::Ship)] == 12);
        CHECK(c->region.enabled && c->region.startPct == 40 && c->region.endPct == 60 && c->region.extraTicks == 12);
        CHECK(c->adaptive);
    }
}

static void testConfigCodeV3BigTicks() {
    std::puts("[config] TS2 v3 guarda ticks hasta 255 y las opciones nuevas");
    Config cfg;
    cfg.globalMaxTicks = 255;
    cfg.modeMaxTicks[idx(Mode::Swing)] = 200;
    cfg.region = Region{true, 10, 90, 250};
    cfg.dropUnneeded = false;
    cfg.saveReleases = false;
    auto code = encodeConfig(cfg);
    std::printf("  codigo v3 (%zu chars): %s\n", code.size(), code.c_str());
    auto res = decodeConfig(code, Config{});
    CHECK(std::holds_alternative<Config>(res));
    if (auto* c = std::get_if<Config>(&res)) {
        CHECK(c->globalMaxTicks == 255 && c->modeMaxTicks[idx(Mode::Swing)] == 200);
        CHECK(c->region.extraTicks == 250);
        CHECK(!c->dropUnneeded && !c->saveReleases);
    }
}

int main() {
    testCleanClick();
    testEarlyClickSaved();
    testPreserveHold();
    testMaxDelayReached();
    testIdleWouldKill();
    testDisabledAndSkipped();
    testDisableWhileHolding();
    testRetapDropsOldTap();
    testTwoSlotsIndependent();
    testRepressFlushesRelease();
    testConfigCodeRoundtrip();
    testConfigCodeErrors();
    testPresetsAndRegion();
    testAdaptive();
    testStats();
    testUnnecessaryClickNotCounted();
    testDeadlyUnneededClickDropped();
    testNeededEarlyClickStillApplied();
    testDropAtMaxDelay();
    testEarlyReleaseSaved();
    testRepressCancelsHeldRelease();
    testConfigCodeV2StillImports();
    testConfigCodeV3BigTicks();
    std::printf("\n%d/%d checks OK\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
