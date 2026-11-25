#include "ConfigCode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ts {
namespace {

constexpr std::uint8_t kFormatVersion = 2;
constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  // Crockford Base32

// CRC-8/SMBUS: poly 0x07, init 0x00. Suficiente para detectar typos.
std::uint8_t crc8(std::vector<std::uint8_t> const& data) {
    std::uint8_t crc = 0;
    for (auto byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 0x80) ? static_cast<std::uint8_t>((crc << 1) ^ 0x07) : static_cast<std::uint8_t>(crc << 1);
        }
    }
    return crc;
}

std::string base32Encode(std::vector<std::uint8_t> const& data) {
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (auto byte : data) {
        buffer = (buffer << 8) | byte;
        bits += 8;
        while (bits >= 5) {
            out += kAlphabet[(buffer >> (bits - 5)) & 0x1F];
            bits -= 5;
        }
    }
    if (bits > 0) out += kAlphabet[(buffer << (5 - bits)) & 0x1F];
    return out;
}

// -1 si el caracter no es válido. Acepta minúsculas y las confusiones típicas.
int base32Value(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c == 'I' || c == 'L') c = '1';
    if (c == 'O') c = '0';
    for (int i = 0; i < 32; ++i) {
        if (kAlphabet[i] == c) return i;
    }
    return -1;
}

bool base32Decode(std::string_view text, std::vector<std::uint8_t>& out) {
    std::uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '-' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        int v = base32Value(c);
        if (v < 0) return false;
        buffer = (buffer << 5) | static_cast<std::uint32_t>(v);
        bits += 5;
        if (bits >= 8) {
            out.push_back(static_cast<std::uint8_t>((buffer >> (bits - 8)) & 0xFF));
            bits -= 8;
        }
    }
    return true;
}

std::string groupBy5(std::string const& raw) {
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (i > 0 && i % 5 == 0) out += '-';
        out += raw[i];
    }
    return out;
}

bool startsWithPrefixCaseInsensitive(std::string_view s) {
    if (s.size() < kCodePrefix.size()) return false;
    for (std::size_t i = 0; i < kCodePrefix.size(); ++i) {
        char a = s[i];
        if (a >= 'a' && a <= 'z') a = static_cast<char>(a - 'a' + 'A');
        if (a != kCodePrefix[i]) return false;
    }
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\n' || s.front() == '\r' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

} // namespace

std::string encodeConfig(Config const& cfg) {
    bool customPalette = cfg.palette != Config::defaultPalette();

    std::vector<std::uint8_t> bytes;
    bytes.reserve(64);
    bytes.push_back(kFormatVersion);

    std::uint8_t flags = 0;
    flags |= cfg.enabled ? 0x01 : 0;
    flags |= cfg.correctBoth ? 0x02 : 0;
    flags |= cfg.adaptive ? 0x04 : 0;
    flags |= cfg.preserveHold ? 0x08 : 0;
    flags |= cfg.safeMode ? 0x10 : 0;
    flags |= cfg.region.enabled ? 0x20 : 0;
    flags |= customPalette ? 0x40 : 0;
    bytes.push_back(flags);

    bytes.push_back(static_cast<std::uint8_t>(cfg.preset));
    bytes.push_back(cfg.globalMaxTicks);
    for (auto t : cfg.modeMaxTicks) bytes.push_back(t);
    bytes.push_back(cfg.region.startPct);
    bytes.push_back(cfg.region.endPct);
    bytes.push_back(cfg.region.extraTicks);

    std::uint8_t hud = 0;
    hud |= cfg.showHud ? 0x01 : 0;
    hud |= cfg.showTimingBar ? 0x02 : 0;
    hud |= cfg.showDeathSummary ? 0x04 : 0;
    bytes.push_back(hud);
    bytes.push_back(static_cast<std::uint8_t>(std::clamp<long>(std::lround(cfg.hudScale * 50.f), 25, 75)));

    if (customPalette) {
        for (auto const& c : cfg.palette) {
            bytes.push_back(c.r);
            bytes.push_back(c.g);
            bytes.push_back(c.b);
            bytes.push_back(c.a);
        }
    }
    bytes.push_back(crc8(bytes));

    return std::string(kCodePrefix) + groupBy5(base32Encode(bytes));
}

std::variant<Config, CodeError> decodeConfig(std::string_view code, Config const& base) {
    code = trim(code);
    if (!startsWithPrefixCaseInsensitive(code)) {
        return CodeError{"El codigo debe empezar con TS2-"};
    }
    code.remove_prefix(kCodePrefix.size());

    std::vector<std::uint8_t> bytes;
    if (!base32Decode(code, bytes)) return CodeError{"El codigo tiene caracteres invalidos"};

    // Tamaños válidos: 17 bytes de datos (+32 de paleta) + 1 de CRC.
    constexpr std::size_t kBase = 17;
    if (bytes.size() < kBase + 1) return CodeError{"Codigo incompleto"};

    bool customPalette = (bytes[1] & 0x40) != 0;
    std::size_t expected = kBase + (customPalette ? 32 : 0) + 1;
    if (bytes.size() < expected) return CodeError{"Codigo incompleto"};
    bytes.resize(expected);  // descarta bits de relleno del Base32

    std::uint8_t storedCrc = bytes.back();
    bytes.pop_back();
    if (crc8(bytes) != storedCrc) return CodeError{"Codigo danado (checksum invalido)"};
    if (bytes[0] != kFormatVersion) return CodeError{"Version de codigo no soportada"};

    Config cfg = base;
    std::uint8_t flags = bytes[1];
    cfg.enabled = flags & 0x01;
    cfg.correctBoth = flags & 0x02;
    cfg.adaptive = flags & 0x04;
    cfg.preserveHold = flags & 0x08;
    cfg.safeMode = flags & 0x10;
    cfg.region.enabled = flags & 0x20;

    cfg.preset = bytes[2] < kPresetCount ? static_cast<Preset>(bytes[2]) : Preset::Custom;
    auto clampTicks = [](std::uint8_t v) { return static_cast<std::uint8_t>(std::min<int>(v, kMaxSaveTicks)); };
    cfg.globalMaxTicks = clampTicks(bytes[3]);
    for (std::size_t i = 0; i < kModeCount; ++i) cfg.modeMaxTicks[i] = clampTicks(bytes[4 + i]);

    cfg.region.startPct = std::min<std::uint8_t>(bytes[12], 100);
    cfg.region.endPct = std::min<std::uint8_t>(bytes[13], 100);
    cfg.region.extraTicks = clampTicks(bytes[14]);
    if (cfg.region.endPct <= cfg.region.startPct) cfg.region.enabled = false;

    cfg.showHud = bytes[15] & 0x01;
    cfg.showTimingBar = bytes[15] & 0x02;
    cfg.showDeathSummary = bytes[15] & 0x04;
    cfg.hudScale = std::clamp(static_cast<float>(bytes[16]) / 50.f, 0.5f, 1.5f);

    if (customPalette) {
        for (std::size_t i = 0; i < kPaletteSize; ++i) {
            std::size_t o = kBase + i * 4;
            cfg.palette[i] = RGBA{bytes[o], bytes[o + 1], bytes[o + 2], bytes[o + 3]};
        }
    }
    else {
        cfg.palette = Config::defaultPalette();
    }

    // Si los valores no coinciden con el preset declarado, es "Personalizado".
    if (cfg.preset != Preset::Custom && detectPreset(cfg) != cfg.preset) cfg.preset = Preset::Custom;
    return cfg;
}

} // namespace ts
