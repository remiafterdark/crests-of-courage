

#include "mod.hpp"
#include "net/messages.hpp"

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/host.h"
#include "mods/svc/log.hpp"
#include "print.hpp"
#include "mods/svc/texture.h"

#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphBase/J3DPacket.h"
#include "JSystem/J3DGraphBase/J3DTexture.h"
#include "JSystem/JUtility/JUTNameTab.h"
#include "d/d_com_inf_game.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace {

enum ColorSlot : uint8_t {
    kSlotOrdonShirt,
    kSlotOrdonPants,
    kSlotOrdonBelt,
    kSlotHeroCap,
    kSlotHeroBody,
    kSlotHeroSkirt,
    kSlotZoraCap,
    kSlotZoraHelmet,
    kSlotZoraTorso,
    kSlotZoraScales,
    kSlotZoraFlippers,
    kSlotMagicArmor,
    kSlotMagicTrim,
    kSlotMagicBoots,
    kSlotMagicTiara,
    kSlotHair,
    kSlotWolf,
    kSlotIronBoots,
    kSlotCount,
};

struct SlotInfo {
    const char* var;
    const char* group;
    const char* label;
    const char* cosmeticsName;
};

const SlotInfo kSlots[kSlotCount] = {

    {"color_ordon_shirt", "Ordon Clothes", "Shirt", nullptr},
    {"color_ordon_pants", "Ordon Clothes", "Pants", nullptr},
    {"color_ordon_belt", "Ordon Clothes", "Belt", nullptr},
    {"color_hero_cap", "Hero's Tunic", "Cap", "herosTunicCapColor"},
    {"color_hero_body", "Hero's Tunic", "Body", "herosTunicTorsoColor"},
    {"color_hero_skirt", "Hero's Tunic", "Skirt", "herosTunicSkirtColor"},
    {"color_zora_cap", "Zora Armor", "Cap", "zoraArmorCapColor"},
    {"color_zora_helmet", "Zora Armor", "Helmet", "zoraArmorHelmetColor"},
    {"color_zora_torso", "Zora Armor", "Torso", "zoraArmorTorsoColor"},
    {"color_zora_scales", "Zora Armor", "Scales", "zoraArmorScalesColor"},
    {"color_zora_flippers", "Zora Armor", "Flippers", "zoraArmorFlippersColor"},
    {"color_magic_armor", "Magic Armor", "Plate", "magicArmorPlateColor"},
    {"color_magic_trim", "Magic Armor", "Trim", "magicArmorTrimColor"},
    {"color_magic_boots", "Magic Armor", "Boots", "magicArmorBootsColor"},
    {"color_magic_tiara", "Magic Armor", "Tiara", "magicArmorTiaraColor"},
    {"color_hair", "Link", "Hair", "linkHairColor"},
    {"color_wolf", "Wolf Link", "Fur", "wolfLinkColor"},
    {"color_iron_boots", "Equipment", "Iron Boots", "ironBootsColor"},
};

struct TexSlot {
    const char* texture;
    ColorSlot slot;
};

const TexSlot kTextureSlots[] = {
    {"bl_upbody", kSlotOrdonShirt}, {"bl_lowbody", kSlotOrdonPants}, {"al_belt", kSlotOrdonBelt},
    {"al_cap", kSlotHeroCap},       {"al_upbody", kSlotHeroBody},  {"al_lowbody", kSlotHeroSkirt},
    {"zl_cap", kSlotZoraCap},       {"zl_helmet", kSlotZoraHelmet}, {"zl_armor", kSlotZoraTorso},
    {"zl_armL", kSlotZoraTorso},    {"zl_body", kSlotZoraScales},  {"zl_boots", kSlotZoraFlippers},

    {"ml_armor", kSlotMagicArmor},  {"ml_body", kSlotMagicArmor},  {"ml_gauntlet", kSlotMagicTrim},
    {"ml_belts", kSlotMagicTrim},   {"ml_accessory", kSlotMagicTrim},
    {"ml_boots", kSlotMagicBoots},  {"ml_tiara", kSlotMagicTiara}, {"ml_cap", kSlotMagicTiara},
    {"bl_hair", kSlotHair},         {"al_hair", kSlotHair},        {"wl_body", kSlotWolf},
    {"wl_eye.1", kSlotWolf},        {"wl_eye.2", kSlotWolf},       {"wl_eye.3", kSlotWolf},
    {"wl_eye.4", kSlotWolf},        {"wl_eye.5", kSlotWolf},       {"al_bootsH", kSlotIronBoots},
};

int slot_for_texture(const char* name) {
    if (name == nullptr) return -1;
    for (const TexSlot& t : kTextureSlots) {
        if (std::strcmp(t.texture, name) == 0) return t.slot;
    }
    return -1;
}

struct SlotColor {
    bool set = false;
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const SlotColor& o) const {
        return set == o.set && (!set || (r == o.r && g == o.g && b == o.b));
    }
    bool operator!=(const SlotColor& o) const { return !(*this == o); }
};

ConfigVarHandle s_vars[kSlotCount] = {};
SlotColor s_cosmetics[kSlotCount];
SlotColor s_lastSent[kSlotCount];
bool s_needSend = false;

SlotColor s_peer[kCoopMaxPlayers][kSlotCount];

bool parse_hex_color(const std::string& in, SlotColor& out) {
    std::string s = in;
    if (!s.empty() && s[0] == '#') s.erase(s.begin());
    if (s.size() != 6) return false;
    uint32_t value = 0;
    for (char c : s) {
        value <<= 4;
        if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
        else return false;
    }
    out.set = true;
    out.r = static_cast<uint8_t>(value >> 16);
    out.g = static_cast<uint8_t>(value >> 8);
    out.b = static_cast<uint8_t>(value);
    return true;
}

std::string get_var_string(ConfigVarHandle handle) {
    if (handle == 0) return {};
    char buffer[64] = {};
    size_t length = 0;
    if (svc_config->get_string(mod_ctx, handle, buffer, sizeof(buffer), &length) != MOD_OK) return {};
    return std::string(buffer);
}

SlotColor mine_from_var(int slot) {
    SlotColor c;
    parse_hex_color(get_var_string(s_vars[slot]), c);
    return c;
}

SlotColor effective_local(int slot) {
    SlotColor mine = mine_from_var(slot);
    return mine.set ? mine : s_cosmetics[slot];
}

std::filesystem::path s_configJsonPath;
bool s_haveConfigPath = false;

void find_config_json() {
    if (!SERVICE_HAS(svc_host, HostService, data_dir) || svc_host->data_dir == nullptr) return;
    const char* dir = nullptr;
    if (svc_host->data_dir(mod_ctx, &dir) != MOD_OK || dir == nullptr) return;
    std::error_code ec;
    const std::filesystem::path path =
        std::filesystem::u8path(dir).parent_path().parent_path() / "config.json";
    s_configJsonPath = path;
    s_haveConfigPath = true;

    const auto u8 = path.u8string();
    coop_log::info("coop_mod: [COLORS] reading cosmetics colors from {}",
        std::string(u8.begin(), u8.end()));
}

bool json_find_value(const std::string& text, const std::string& key, std::string& out) {
    const std::string quoted = "\"" + key + "\"";
    size_t pos = text.find(quoted);
    if (pos == std::string::npos) return false;
    pos = text.find(':', pos + quoted.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    if (pos >= text.size()) return false;
    if (text[pos] == '"') {
        const size_t end = text.find('"', pos + 1);
        if (end == std::string::npos) return false;
        out = text.substr(pos + 1, end - pos - 1);
        return true;
    }
    size_t end = pos;
    while (end < text.size() && text[end] != ',' && text[end] != '\n' && text[end] != '}') ++end;
    out = text.substr(pos, end - pos);
    while (!out.empty() && (out.back() == ' ' || out.back() == '\r')) out.pop_back();
    return true;
}

void read_cosmetics_colors() {
    for (SlotColor& c : s_cosmetics) c = SlotColor{};
    if (!s_haveConfigPath) return;
    std::ifstream file(s_configJsonPath, std::ios::binary);
    if (!file) return;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string value;
    if (json_find_value(text, "mod.dev_twilitrealm_cosmetics.enabled", value) && value == "false") {
        return;
    }
    for (int i = 0; i < kSlotCount; ++i) {
        if (kSlots[i].cosmeticsName == nullptr) continue;
        const std::string key = std::string("mod.dev_twilitrealm_cosmetics.") + kSlots[i].cosmeticsName;
        if (json_find_value(text, key, value)) parse_hex_color(value, s_cosmetics[i]);
    }
}

constexpr uint64_t kP1 = 11400714785074694791ULL;
constexpr uint64_t kP2 = 14029467366897019727ULL;
constexpr uint64_t kP3 = 1609587929392839161ULL;
constexpr uint64_t kP4 = 9650029242287828579ULL;
constexpr uint64_t kP5 = 2870177450012600261ULL;

inline uint64_t rotl64(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
inline uint64_t read64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
inline uint32_t read32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline uint64_t xxh_round(uint64_t acc, uint64_t input) {
    acc += input * kP2;
    acc = rotl64(acc, 31);
    return acc * kP1;
}
inline uint64_t xxh_merge(uint64_t acc, uint64_t val) {
    acc ^= xxh_round(0, val);
    return acc * kP1 + kP4;
}

uint64_t xxh64(const uint8_t* p, size_t len, uint64_t seed) {
    const uint8_t* const end = p + len;
    uint64_t h;
    if (len >= 32) {
        const uint8_t* const limit = end - 32;
        uint64_t v1 = seed + kP1 + kP2, v2 = seed + kP2, v3 = seed, v4 = seed - kP1;
        do {
            v1 = xxh_round(v1, read64(p)); p += 8;
            v2 = xxh_round(v2, read64(p)); p += 8;
            v3 = xxh_round(v3, read64(p)); p += 8;
            v4 = xxh_round(v4, read64(p)); p += 8;
        } while (p <= limit);
        h = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
        h = xxh_merge(h, v1);
        h = xxh_merge(h, v2);
        h = xxh_merge(h, v3);
        h = xxh_merge(h, v4);
    } else {
        h = seed + kP5;
    }
    h += static_cast<uint64_t>(len);
    while (p + 8 <= end) {
        h ^= xxh_round(0, read64(p));
        h = rotl64(h, 27) * kP1 + kP4;
        p += 8;
    }
    if (p + 4 <= end) {
        h ^= static_cast<uint64_t>(read32(p)) * kP1;
        h = rotl64(h, 23) * kP2 + kP3;
        p += 4;
    }
    while (p < end) {
        h ^= static_cast<uint64_t>(*p) * kP5;
        h = rotl64(h, 11) * kP1;
        ++p;
    }
    h ^= h >> 33;
    h *= kP2;
    h ^= h >> 29;
    h *= kP3;
    h ^= h >> 32;
    return h;
}

inline uint16_t rd16be(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
inline void wr16be(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v); }
inline uint32_t rd32be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}
inline void wr32be(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24); p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);  p[3] = static_cast<uint8_t>(v);
}

void tile_info(uint32_t format, uint32_t& tw, uint32_t& th, uint32_t& ts) {
    switch (format) {
    case GX_TF_I8: case GX_TF_IA4: case GX_TF_C8: tw = 8; th = 4; ts = 32; break;
    case GX_TF_IA8: case GX_TF_RGB565: case GX_TF_RGB5A3: case GX_TF_C14X2: tw = 4; th = 4; ts = 32; break;
    case GX_TF_RGBA8: tw = 4; th = 4; ts = 64; break;
    default: tw = 8; th = 8; ts = 32; break;
    }
}

uint32_t image_data_size(uint32_t format, uint32_t width, uint32_t height, uint32_t mips) {
    uint32_t tw, th, ts;
    tile_info(format, tw, th, ts);
    uint32_t total = 0;
    for (uint32_t i = 0; i < mips; ++i) {
        const uint32_t pw = (width + tw - 1) & ~(tw - 1);
        const uint32_t ph = (height + th - 1) & ~(th - 1);
        total += (pw / tw) * (ph / th) * ts;
        width = width > 1 ? width >> 1 : 1;
        height = height > 1 ? height >> 1 : 1;
    }
    return total;
}

uint8_t desaturate_565(uint16_t v) {
    const uint32_t r = (v & 0xf800) >> 11, g = (v & 0x7e0) >> 5, b = v & 0x1f;
    const uint32_t combined = 30480413 * r + 49085341 * g + 8312839 * b;
    uint8_t shifted = (combined >> 24) & 0xff;
    if (shifted < 0xff && (combined & 0x00800000)) shifted += 1;
    return shifted;
}

uint16_t overlay_565(uint8_t gray, const SlotColor& c) {
    uint32_t r255, g255, b255;
    if (gray <= 0x7f) {
        const uint32_t twice = 2 * gray;
        r255 = twice * c.r; g255 = twice * c.g; b255 = twice * c.b;
    } else {
        const uint32_t m = 2 * (255 - gray);
        r255 = 255 * 255 - m * (255 - c.r);
        g255 = 255 * 255 - m * (255 - c.g);
        b255 = 255 * 255 - m * (255 - c.b);
    }
    const uint32_t r = (r255 + 1 + (r255 >> 8)) >> 8;
    const uint32_t g = (g255 + 1 + (g255 >> 8)) >> 8;
    const uint32_t b = (b255 + 1 + (b255 >> 8)) >> 8;
    return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | ((b & 0xf8) >> 3));
}

uint8_t overlay_channel(uint8_t base, uint8_t blend) {
    if (base < 128) return static_cast<uint8_t>((2 * base * blend) / 255);
    return static_cast<uint8_t>(255 - (2 * (255 - base) * (255 - blend)) / 255);
}

uint32_t swap_index_bits(bool leftIsGreater, uint32_t bits) {
    if (leftIsGreater) return bits ^ 0x55555555;
    const uint32_t mask = ((bits >> 1) & 0x55555555) ^ 0x55555555;
    return bits ^ mask;
}

void recolor_cmpr(std::vector<uint8_t>& data, uint32_t width, uint32_t height, uint32_t mips,
    const SlotColor& color) {
    uint16_t table[0x100];
    for (int i = 0; i < 0x100; ++i) table[i] = overlay_565(static_cast<uint8_t>(i), color);
    uint8_t* cur = data.data();
    const uint8_t* const end = data.data() + data.size();
    for (uint32_t mip = 0; mip < mips; ++mip) {
        const uint32_t rw = (width + 7) & ~7u, rh = (height + 7) & ~7u;
        const uint32_t iterations = (rw / 8) * (rh / 8) * 4;
        for (uint32_t i = 0; i < iterations && cur + 8 <= end; ++i) {
            const uint16_t left = rd16be(cur), right = rd16be(cur + 2);
            const bool leftIsGreater = left > right;
            uint16_t newLeft = table[desaturate_565(left)];
            uint16_t newRight = table[desaturate_565(right)];
            bool swap = false;
            if (leftIsGreater) {
                if (newLeft == newRight) {
                    if ((newLeft & 0x1f) == 0) newLeft += 1;
                    newRight = static_cast<uint16_t>(newLeft - 1);
                } else if (newLeft < newRight) {
                    swap = true;
                }
            } else if (newLeft > newRight) {
                swap = true;
            }
            if (swap) {
                std::swap(newLeft, newRight);
                wr32be(cur + 4, swap_index_bits(leftIsGreater, rd32be(cur + 4)));
            }
            wr16be(cur, newLeft);
            wr16be(cur + 2, newRight);
            cur += 8;
        }
        width = width > 1 ? width >> 1 : 1;
        height = height > 1 ? height >> 1 : 1;
    }
}

void recolor_rgb5a3(std::vector<uint8_t>& data, uint32_t width, uint32_t height, uint32_t mips,
    const SlotColor& color) {
    uint16_t t555[0x100], t444[0x100];
    for (int i = 0; i < 0x100; ++i) {
        const uint8_t r = overlay_channel(static_cast<uint8_t>(i), color.r);
        const uint8_t g = overlay_channel(static_cast<uint8_t>(i), color.g);
        const uint8_t b = overlay_channel(static_cast<uint8_t>(i), color.b);
        t555[i] = static_cast<uint16_t>(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
        t444[i] = static_cast<uint16_t>(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
    }
    uint8_t* px = data.data();
    const uint8_t* const end = data.data() + data.size();
    for (uint32_t mip = 0; mip < mips; ++mip) {
        const uint32_t total = ((width + 3) & ~3u) * ((height + 3) & ~3u);
        for (uint32_t i = 0; i < total && px + 2 <= end; ++i, px += 2) {
            const uint16_t raw = rd16be(px);
            if (raw & 0x8000) {
                const uint8_t r5 = (raw >> 10) & 0x1f, g5 = (raw >> 5) & 0x1f, b5 = raw & 0x1f;
                const uint8_t r8 = (r5 << 3) | (r5 >> 2), g8 = (g5 << 3) | (g5 >> 2), b8 = (b5 << 3) | (b5 >> 2);
                wr16be(px, t555[static_cast<uint8_t>((r8 * 77 + g8 * 150 + b8 * 29) >> 8)]);
            } else {
                const uint16_t alpha = raw & 0x7000;
                const uint8_t r4 = (raw >> 8) & 0xf, g4 = (raw >> 4) & 0xf, b4 = raw & 0xf;
                const uint8_t r8 = (r4 << 4) | r4, g8 = (g4 << 4) | g4, b8 = (b4 << 4) | b4;
                wr16be(px, static_cast<uint16_t>(alpha | t444[static_cast<uint8_t>((r8 * 77 + g8 * 150 + b8 * 29) >> 8)]));
            }
        }
        width = width > 1 ? width >> 1 : 1;
        height = height > 1 ? height >> 1 : 1;
    }
}

void encode_cmpr_sub_block(uint8_t* dst, const uint8_t pixels[16]) {
    uint8_t minV = 255, maxV = 0;
    for (int i = 0; i < 16; ++i) {
        if (pixels[i] < minV) minV = pixels[i];
        if (pixels[i] > maxV) maxV = pixels[i];
    }
    auto to565 = [](uint8_t v) -> uint16_t {
        return static_cast<uint16_t>(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
    };
    uint16_t c0 = to565(maxV), c1 = to565(minV);
    uint32_t indices = 0;
    if (maxV > minV) {
        if (c0 == c1) {
            if ((c0 & 0x1f) < 0x1f) c0 += 1; else c1 -= 1;
        }
        const int p0 = maxV, p1 = minV, p2 = (2 * maxV + minV) / 3, p3 = (maxV + 2 * minV) / 3;
        for (int i = 0; i < 16; ++i) {
            const int p = pixels[i];
            int best = 0, bestD = std::abs(p - p0);
            if (std::abs(p - p1) < bestD) { bestD = std::abs(p - p1); best = 1; }
            if (std::abs(p - p2) < bestD) { bestD = std::abs(p - p2); best = 2; }
            if (std::abs(p - p3) < bestD) { bestD = std::abs(p - p3); best = 3; }
            indices |= static_cast<uint32_t>(best) << (30 - 2 * i);
        }
    }
    wr16be(dst, c0);
    wr16be(dst + 2, c1);
    wr32be(dst + 4, indices);
}

bool i8_to_cmpr(std::vector<uint8_t>& data, uint32_t width, uint32_t height, uint32_t mips) {
    if (data.size() < image_data_size(GX_TF_I8, width, height, mips)) return false;
    const std::vector<uint8_t> src = data;
    std::vector<uint8_t> out(image_data_size(GX_TF_CMPR, width, height, mips));
    const uint8_t* rd = src.data();
    uint8_t* wr = out.data();
    for (uint32_t mip = 0; mip < mips; ++mip) {
        const uint32_t tilesX = ((width + 7) & ~7u) / 8;
        const uint32_t tilesY = ((height + 3) & ~3u) / 4;
        const uint32_t blocksX = ((width + 7) & ~7u) / 8, blocksY = ((height + 7) & ~7u) / 8;
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                uint8_t sub[4][16] = {};
                for (uint32_t sy = 0; sy < 2; ++sy) {
                    for (uint32_t sx = 0; sx < 2; ++sx) {
                        for (uint32_t row = 0; row < 4; ++row) {
                            for (uint32_t col = 0; col < 4; ++col) {
                                const uint32_t x = bx * 8 + sx * 4 + col, y = by * 8 + sy * 4 + row;
                                if (x >= width || y >= height) continue;
                                const uint32_t tile = (y / 4) * tilesX + (x / 8);
                                sub[sy * 2 + sx][row * 4 + col] = rd[tile * 32 + (y % 4) * 8 + (x % 8)];
                            }
                        }
                    }
                }
                for (auto& s : sub) {
                    encode_cmpr_sub_block(wr, s);
                    wr += 8;
                }
            }
        }
        rd += tilesX * tilesY * 32;
        width = width > 1 ? width >> 1 : 1;
        height = height > 1 ? height >> 1 : 1;
    }
    data.swap(out);
    return true;
}

bool recolor(std::vector<uint8_t>& data, uint32_t& format, uint32_t width, uint32_t height,
    uint32_t mips, const SlotColor& color) {
    switch (format) {
    case GX_TF_CMPR:
        recolor_cmpr(data, width, height, mips, color);
        return true;
    case GX_TF_RGB5A3:
        recolor_rgb5a3(data, width, height, mips, color);
        return true;
    case GX_TF_I8:
        if (!i8_to_cmpr(data, width, height, mips)) return false;
        format = GX_TF_CMPR;
        recolor_cmpr(data, width, height, mips, color);
        return true;
    default:
        return false;
    }
}

uint32_t mip_count_of(const ResTIMG* timg) {
    return timg->mipmapEnabled ? (timg->mipmapCount > 0 ? timg->mipmapCount : 1) : 1;
}

struct SelfTexture {
    const char* arc;
    const char* bmd;
    const char* texture;
    ColorSlot slot;
    bool loaded = false;
    uint32_t width = 0, height = 0, format = 0, mips = 1;
    bool hasTlut = false;
    uint64_t hash = 0;
    std::vector<uint8_t> base;
    TextureReplacementHandle handle = 0;
    SlotColor applied;
};

std::vector<SelfTexture> s_self;

void build_self_table() {
    auto add = [](const char* arc, const char* bmd, const char* tex, ColorSlot slot) {
        SelfTexture t;
        t.arc = arc;
        t.bmd = bmd;
        t.texture = tex;
        t.slot = slot;
        s_self.push_back(std::move(t));
    };
    add("Bmdl", "bl.bmd", "bl_upbody", kSlotOrdonShirt);
    add("Bmdl", "bl.bmd", "bl_lowbody", kSlotOrdonPants);
    add("Bmdl", "bl.bmd", "al_belt", kSlotOrdonBelt);
    add("Kmdl", "al_head.bmd", "al_cap", kSlotHeroCap);
    add("Kmdl", "al.bmd", "al_upbody", kSlotHeroBody);
    add("Kmdl", "al.bmd", "al_lowbody", kSlotHeroSkirt);
    add("Zmdl", "zl_head.bmd", "zl_cap", kSlotZoraCap);
    add("Zmdl", "zl_head.bmd", "zl_helmet", kSlotZoraHelmet);
    add("Zmdl", "zl.bmd", "zl_armor", kSlotZoraTorso);
    add("Zmdl", "zl.bmd", "zl_armL", kSlotZoraTorso);
    add("Zmdl", "zl.bmd", "zl_body", kSlotZoraScales);
    add("Zmdl", "zl.bmd", "zl_boots", kSlotZoraFlippers);
    add("Bmdl", "bl_head.bmd", "bl_hair", kSlotHair);
    add("Kmdl", "al_head.bmd", "al_hair", kSlotHair);
    add("Mmdl", "ml.bmd", "ml_armor", kSlotMagicArmor);
    add("Mmdl", "ml.bmd", "ml_body", kSlotMagicArmor);
    add("Mmdl", "ml.bmd", "ml_gauntlet", kSlotMagicTrim);
    add("Mmdl", "ml.bmd", "ml_belts", kSlotMagicTrim);
    add("Mmdl", "ml.bmd", "ml_accessory", kSlotMagicTrim);
    add("Mmdl", "ml.bmd", "ml_boots", kSlotMagicBoots);
    add("Mmdl", "ml_head.bmd", "ml_tiara", kSlotMagicTiara);
    add("Mmdl", "ml_head.bmd", "ml_cap", kSlotMagicTiara);
    add("Mmdl", "ml_head.bmd", "al_hair", kSlotHair);
    add("Wmdl", "wl.bmd", "wl_body", kSlotWolf);
    add("Wmdl", "wl.bmd", "wl_eye.1", kSlotWolf);
    add("Wmdl", "wl.bmd", "wl_eye.2", kSlotWolf);
    add("Wmdl", "wl.bmd", "wl_eye.3", kSlotWolf);
    add("Wmdl", "wl.bmd", "wl_eye.4", kSlotWolf);
    add("Wmdl", "wl.bmd", "wl_eye.5", kSlotWolf);
    add("Bmdl", "al_bootsh.bmd", "al_bootsH", kSlotIronBoots);
    add("Kmdl", "al_bootsh.bmd", "al_bootsH", kSlotIronBoots);
    add("Zmdl", "al_bootsh.bmd", "al_bootsH", kSlotIronBoots);
    add("Mmdl", "al_bootsh.bmd", "al_bootsH", kSlotIronBoots);
    add("O_gD_boot", "o_gd_al_bootsh.bmd", "al_bootsH", kSlotIronBoots);
}

void load_self_base_textures() {
    for (SelfTexture& t : s_self) {
        if (t.loaded) continue;
        dRes_info_c* info = dComIfG_getObjectResInfo(t.arc);
        if (info == nullptr || info->getArchive() == nullptr) continue;

        local_skin_suppress(true);
        J3DModelData* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(t.arc, t.bmd));
        local_skin_suppress(false);
        if (data == nullptr) continue;
        J3DTexture* tex = data->getTexture();
        JUTNameTab* names = data->getTextureName();
        if (tex == nullptr || names == nullptr) continue;
        for (u16 i = 0; i < tex->getNum(); ++i) {
            const char* name = names->getName(i);
            if (name == nullptr || std::strcmp(name, t.texture) != 0) continue;
            const ResTIMG* timg = tex->getResTIMG(i);
            t.width = timg->width;
            t.height = timg->height;
            t.format = timg->format;
            t.mips = mip_count_of(timg);
            t.hasTlut = timg->numColors > 0;
            const uint32_t size = image_data_size(t.format, t.width, t.height, t.mips);
            t.base.assign(tex->getImgDataPtr(i), tex->getImgDataPtr(i) + size);
            t.hash = xxh64(t.base.data(), image_data_size(t.format, t.width, t.height, 1), 0);
            t.loaded = true;
            break;
        }
    }
}

void update_self_colors() {
    if (svc_texture == nullptr) return;
    for (SelfTexture& t : s_self) {
        if (!t.loaded) continue;
        const SlotColor want = mine_from_var(t.slot);
        if (want == t.applied && (want.set == (t.handle != 0))) continue;
        if (!want.set) {
            if (t.handle != 0) svc_texture->unregister(mod_ctx, t.handle);
            t.handle = 0;
            t.applied = SlotColor{};
            continue;
        }
        std::vector<uint8_t> pixels = t.base;
        uint32_t format = t.format;
        if (!recolor(pixels, format, t.width, t.height, t.mips, want)) {
            t.applied = want;
            continue;
        }
        TextureKey key = TEXTURE_KEY_INIT;
        key.kind = TEXTURE_KEY_SOURCE;
        key.texture_hash = t.hash;
        key.tlut_hash = 0;
        key.width = t.width;
        key.height = t.height;
        key.gx_format = t.format;
        key.has_tlut = t.hasTlut;
        TextureData data = TEXTURE_DATA_INIT;
        data.data = pixels.data();
        data.size = pixels.size();
        data.width = t.width;
        data.height = t.height;
        data.mip_count = t.mips;
        data.gx_format = format;
        TextureReplacementHandle handle = 0;
        if (svc_texture->register_data(mod_ctx, &key, &data, &handle) != MOD_OK) continue;
        if (t.handle != 0) svc_texture->unregister(mod_ctx, t.handle);
        t.handle = handle;
        t.applied = want;
    }
}

struct J3DTextureMirror {
    void* vtable;
    u16 num;
    u16 unk;
    ResTIMG* res;
    void* tlutObj;
    TGXTexObj* texObj;
    u8** imgData;
    u8** tlutData;
};
static_assert(sizeof(J3DTextureMirror) == sizeof(J3DTexture), "J3DTexture layout changed");

struct PuppetTexture {
    u16 index = 0;
    uint8_t slot = 0;
    uint8_t* buffer = nullptr;
    uint32_t size = 0;
    uint32_t width = 0, height = 0, format = 0, mips = 1;
    TextureReplacementHandle handle = 0;
    SlotColor applied;
    bool registered = false;
};

const int kMaxPuppetTextures = 8;
struct PuppetModelColors {
    J3DModel* model = nullptr;

    bool mine = false;
    uint8_t owner = kCoopNoPlayer;
    unsigned char* shadow = nullptr;
    TGXTexObj* texObjs = nullptr;
    u8** imgPtrs = nullptr;
    u16 texNum = 0;
    int count = 0;
    PuppetTexture tex[kMaxPuppetTextures];
};

const int kMaxPuppetModels = kCoopMaxPlayers * 4 + 4;
PuppetModelColors s_models[kMaxPuppetModels];

SlotColor peer_color(uint8_t owner, int slot) {
    if (!coop_net_connected() || slot < 0 || slot >= kSlotCount) return SlotColor{};
    if (owner >= kCoopMaxPlayers) return SlotColor{};
    return s_peer[owner][slot];
}

void apply_puppet_texture(PuppetModelColors& m, PuppetTexture& t) {
    if (svc_texture == nullptr) return;
    const SlotColor want = m.mine ? effective_local(t.slot) : peer_color(m.owner, t.slot);
    if (t.registered && want == t.applied) return;

    std::vector<uint8_t> pixels(t.buffer, t.buffer + t.size);
    uint32_t format = t.format;
    if (want.set && !recolor(pixels, format, t.width, t.height, t.mips, want)) {

        pixels.assign(t.buffer, t.buffer + t.size);
        format = t.format;
    }
    TextureKey key = TEXTURE_KEY_INIT;
    key.kind = TEXTURE_KEY_POINTER;
    key.pointer = t.buffer;
    key.width = t.width;
    key.height = t.height;
    key.gx_format = t.format;
    TextureData data = TEXTURE_DATA_INIT;
    data.data = pixels.data();
    data.size = pixels.size();
    data.width = t.width;
    data.height = t.height;
    data.mip_count = t.mips;
    data.gx_format = format;
    TextureReplacementHandle handle = 0;
    if (svc_texture->register_data(mod_ctx, &key, &data, &handle) != MOD_OK) {
        coop_log::warn("coop_mod: [COLORS] pointer replacement failed for slot {}", t.slot);
        t.applied = want;
        t.registered = true;
        return;
    }
    if (t.handle != 0) svc_texture->unregister(mod_ctx, t.handle);
    t.handle = handle;
    t.applied = want;
    t.registered = true;

    GXDestroyTexObj(&m.texObjs[t.index]);
    reinterpret_cast<J3DTexture*>(m.shadow)->loadGXTexObj(t.index);
}

void release_model_colors(PuppetModelColors& m) {

    if (m.model != nullptr && coop_ptr_looks_live(m.model)) {
        J3DModelData* data = m.model->getModelData();
        if (coop_ptr_looks_live(data)) {
            J3DTexture* original = data->getTexture();
            if (coop_ptr_looks_live(original)) {
                const u16 matNum = data->getMaterialNum();
                for (u16 i = 0; i < matNum; ++i) {
                    J3DMatPacket* packet = m.model->getMatPacket(i);
                    if (packet != nullptr) packet->setTexture(original);
                }
            }
        }
    }
    for (int i = 0; i < m.count; ++i) {
        PuppetTexture& t = m.tex[i];
        if (t.handle != 0 && svc_texture != nullptr) svc_texture->unregister(mod_ctx, t.handle);
        if (t.registered && m.texObjs != nullptr) GXDestroyTexObj(&m.texObjs[t.index]);
        std::free(t.buffer);
    }
    std::free(m.texObjs);
    std::free(m.imgPtrs);
    std::free(m.shadow);
    m = PuppetModelColors{};
}

}

bool coop_config_json_value(const char* key, std::string* out) {
    if (key == nullptr || out == nullptr) return false;
    if (!s_haveConfigPath) find_config_json();
    if (!s_haveConfigPath) return false;
    std::ifstream file(s_configJsonPath, std::ios::binary);
    if (!file) return false;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return json_find_value(text, key, *out);
}

void colors_register_vars() {
    for (int i = 0; i < kSlotCount; ++i) {
        ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
        desc.name = kSlots[i].var;
        desc.type = CONFIG_VAR_STRING;
        desc.default_string = "";
        if (svc_config->register_var(mod_ctx, &desc, &s_vars[i]) != MOD_OK) s_vars[i] = 0;
    }
}

void colors_init() {
    build_self_table();
    find_config_json();
    read_cosmetics_colors();
    if (svc_texture == nullptr) {
        coop_log::warn("coop_mod: [COLORS] texture service unavailable - colors disabled");
    }
}

int colors_slot_count() {
    return kSlotCount;
}

const char* colors_slot_label(int slot) {
    return (slot >= 0 && slot < kSlotCount) ? kSlots[slot].label : "";
}

const char* colors_slot_group(int slot) {
    return (slot >= 0 && slot < kSlotCount) ? kSlots[slot].group : "";
}

ConfigVarHandle colors_slot_var(int slot) {
    return (slot >= 0 && slot < kSlotCount) ? s_vars[slot] : 0;
}

void colors_invalidate_self() {
    for (SelfTexture& t : s_self) {
        if (t.handle != 0 && svc_texture != nullptr) svc_texture->unregister(mod_ctx, t.handle);
        t.handle = 0;
        t.loaded = false;
        t.applied = SlotColor{};
    }
}

void colors_reset_mine() {
    for (int i = 0; i < kSlotCount; ++i) {
        if (s_vars[i] != 0) svc_config->set_string(mod_ctx, s_vars[i], "");
    }
}

void colors_update() {
    static uint32_t tick = 0;
    ++tick;

    if (tick % 30 == 0) load_self_base_textures();
    if (tick % 180 == 1) read_cosmetics_colors();
    if (tick % 10 == 0) update_self_colors();

    if (coop_net_connected() && tick % 15 == 0) {
        bool changed = s_needSend;
        SlotColor current[kSlotCount];
        for (int i = 0; i < kSlotCount; ++i) {
            current[i] = effective_local(i);
            if (current[i] != s_lastSent[i]) changed = true;
        }
        if (changed) {
            MsgColorEntry entries[kCoopColorSlots] = {};
            for (int i = 0; i < kSlotCount && i < kCoopColorSlots; ++i) {
                entries[i].set = current[i].set ? 1 : 0;
                entries[i].r = current[i].r;
                entries[i].g = current[i].g;
                entries[i].b = current[i].b;
                s_lastSent[i] = current[i];
            }
            coop_net_send(kMsgColors, entries, sizeof(entries));
            s_needSend = false;
        }
    }

    for (PuppetModelColors& m : s_models) {
        if (m.model == nullptr) continue;
        for (int i = 0; i < m.count; ++i) apply_puppet_texture(m, m.tex[i]);
    }
}

void colors_on_connected() {
    s_needSend = true;
    for (auto& row : s_peer) for (SlotColor& c : row) c = SlotColor{};
}

void colors_on_disconnected() {
    for (auto& row : s_peer) for (SlotColor& c : row) c = SlotColor{};
    for (SlotColor& c : s_lastSent) c = SlotColor{};
}

void colors_on_message(const uint8_t* payload, size_t size, uint8_t from) {
    if (from >= kCoopMaxPlayers) return;
    const size_t count = size / sizeof(MsgColorEntry);
    for (size_t i = 0; i < count && i < static_cast<size_t>(kSlotCount); ++i) {
        MsgColorEntry entry;
        std::memcpy(&entry, payload + i * sizeof(MsgColorEntry), sizeof(entry));
        s_peer[from][i].set = entry.set != 0;
        s_peer[from][i].r = entry.r;
        s_peer[from][i].g = entry.g;
        s_peer[from][i].b = entry.b;
    }

    for (PuppetModelColors& m : s_models) {
        if (m.model == nullptr || m.mine || m.owner != from) continue;
        for (int i = 0; i < m.count; ++i) m.tex[i].registered = false;
    }
    coop_log::info("coop_mod: [COLORS] player {} sent their colors ({} slots)",
        static_cast<int>(from), count);
}

void colors_attach_model(J3DModel* model, bool mine, uint8_t owner = kCoopNoPlayer) {
    if (model == nullptr || svc_texture == nullptr) return;
    for (const PuppetModelColors& m : s_models) {
        if (m.model == model) return;
    }
    PuppetModelColors* slot = nullptr;
    for (PuppetModelColors& m : s_models) {
        if (m.model == nullptr) {
            slot = &m;
            break;
        }
    }
    if (slot == nullptr) return;

    if (!coop_ptr_looks_live(model)) return;
    J3DModelData* data = model->getModelData();
    if (!coop_ptr_looks_live(data)) return;
    J3DTexture* original = data->getTexture();
    JUTNameTab* names = data->getTextureName();
    if (!coop_ptr_looks_live(original) || !coop_ptr_looks_live(names)) return;
    if (original->getNum() == 0) return;

    PuppetModelColors m;
    m.model = model;
    m.mine = mine;
    m.owner = mine ? kCoopNoPlayer : owner;
    m.texNum = original->getNum();
    for (u16 i = 0; i < m.texNum && m.count < kMaxPuppetTextures; ++i) {
        const int slotIndex = slot_for_texture(names->getName(i));
        if (slotIndex < 0) continue;
        const ResTIMG* timg = original->getResTIMG(i);
        if (timg->indexTexture || timg->width == 0 || timg->height == 0) continue;
        PuppetTexture& t = m.tex[m.count++];
        t.index = i;
        t.slot = static_cast<uint8_t>(slotIndex);
        t.width = timg->width;
        t.height = timg->height;
        t.format = timg->format;
        t.mips = mip_count_of(timg);
        t.size = image_data_size(t.format, t.width, t.height, t.mips);
    }
    if (m.count == 0) return;

    m.shadow = static_cast<unsigned char*>(std::malloc(sizeof(J3DTexture)));
    m.texObjs = static_cast<TGXTexObj*>(std::malloc(sizeof(TGXTexObj) * m.texNum));
    m.imgPtrs = static_cast<u8**>(std::malloc(sizeof(u8*) * m.texNum));
    bool ok = m.shadow != nullptr && m.texObjs != nullptr && m.imgPtrs != nullptr;
    for (int i = 0; ok && i < m.count; ++i) {
        m.tex[i].buffer = static_cast<uint8_t*>(std::malloc(m.tex[i].size));
        ok = m.tex[i].buffer != nullptr;
    }
    if (!ok) {
        release_model_colors(m);
        return;
    }

    const J3DTextureMirror* src = reinterpret_cast<const J3DTextureMirror*>(original);
    std::memcpy(m.shadow, original, sizeof(J3DTexture));

    std::memcpy(static_cast<void*>(m.texObjs), src->texObj, sizeof(TGXTexObj) * m.texNum);
    std::memcpy(m.imgPtrs, src->imgData, sizeof(u8*) * m.texNum);
    J3DTextureMirror* shadow = reinterpret_cast<J3DTextureMirror*>(m.shadow);
    shadow->texObj = m.texObjs;
    shadow->imgData = m.imgPtrs;

    for (int i = 0; i < m.count; ++i) {
        PuppetTexture& t = m.tex[i];
        std::memcpy(t.buffer, original->getImgDataPtr(t.index), t.size);
        m.imgPtrs[t.index] = t.buffer;
    }

    m.mine = mine;
    *slot = m;
    for (int i = 0; i < slot->count; ++i) apply_puppet_texture(*slot, slot->tex[i]);

    const u16 matNum = data->getMaterialNum();
    for (u16 i = 0; i < matNum; ++i) {
        model->getMatPacket(i)->setTexture(reinterpret_cast<J3DTexture*>(slot->shadow));
    }
    coop_log::info("coop_mod: [COLORS] puppet model {:p}: {} recolorable texture(s)",
        static_cast<void*>(model), slot->count);
}

void colors_attach_puppet_model(J3DModel* model, uint8_t owner) {
    colors_attach_model(model, false, owner);
}

void colors_attach_local_model(J3DModel* model) {
    colors_attach_model(model, true);
}

void colors_detach_model(J3DModel* model) {
    if (model == nullptr) return;
    for (PuppetModelColors& m : s_models) {
        if (m.model == model) release_model_colors(m);
    }
}

void colors_detach_puppet_models() {
    for (PuppetModelColors& m : s_models) {
        if (m.model != nullptr) release_model_colors(m);
    }
}

void colors_detach_local_models() {
    for (PuppetModelColors& m : s_models) {
        if (m.model != nullptr && m.mine) release_model_colors(m);
    }
}
