

#include "mod.hpp"
#include "print.hpp"
#include "util.hpp"
#include "voice_io.hpp"

#include "JSystem/J2DGraph/J2DGrafContext.h"
#include "JSystem/JUtility/JUTFont.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "m_Do/m_Do_ext.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

extern const ConfigService* svc_config;

namespace {

using Clock = std::chrono::steady_clock;

const int kMaxListeners = 8;
const int kMaxHeard = 8;
const int kPreroll = 2;
const auto kHangover = std::chrono::milliseconds(400);
const auto kSpeakingShown = std::chrono::milliseconds(550);
const auto kHeardFor = std::chrono::milliseconds(500);

ConfigVarHandle s_enabledVar = 0;
ConfigVarHandle s_proximityVar = 0;
ConfigVarHandle s_rangeVar = 0;
ConfigVarHandle s_inputVar = 0;

ConfigVarHandle s_inputIndexVar = 0;
ConfigVarHandle s_micVolumeVar = 0;
ConfigVarHandle s_playerVolumeVar = 0;
ConfigVarHandle s_mutedVar = 0;
ConfigVarHandle s_muteKeyVar = 0;

std::unique_ptr<voice_io::Device> s_voice;
std::string s_status = "Off";
uint32_t s_tick = 0;
std::vector<std::string> s_devices;
uint32_t s_devicesTick = 0;
int64_t s_lastIndex = -1;
std::string s_lastName;
bool s_keyCapture = false;
bool s_keyWasDown = false;
bool s_heldAtCapture[256] = {};

std::deque<voice_io::Frame> s_preroll;
Clock::time_point s_talkingUntil{};
Clock::time_point s_localSpeakingUntil{};

struct Speaker {
    Clock::time_point shownUntil{};
    Clock::time_point heardUntil{};
};
std::map<uint8_t, Speaker> s_speakers;
std::set<std::string> s_mutedPeers;

ConfigVarHandle reg(const char* name, ConfigVarType type, int64_t i, bool b, const char* str) {
    ConfigVarDesc d = CONFIG_VAR_DESC_INIT;
    d.name = name;
    d.type = type;
    d.default_int = i;
    d.default_bool = b;
    d.default_string = str;
    ConfigVarHandle h = 0;
    if (svc_config->register_var(mod_ctx, &d, &h) != MOD_OK) h = 0;
    return h;
}

bool enabled() { return cfg_bool(s_enabledVar, false); }
bool mic_muted() { return cfg_bool(s_mutedVar, false); }
bool proximity() { return cfg_bool(s_proximityVar, true); }
int range_percent() { return static_cast<int>(std::clamp<int64_t>(cfg_int(s_rangeVar, 50), 0, 200)); }
int mic_volume() { return static_cast<int>(std::clamp<int64_t>(cfg_int(s_micVolumeVar, 100), 0, 200)); }
int player_volume() { return static_cast<int>(std::clamp<int64_t>(cfg_int(s_playerVolumeVar, 100), 0, 200)); }

bool online() { return global_active() && !coop_net_connected(); }
bool live() { return enabled() && (coop_net_connected() || online()); }

std::string capture_device() {
    std::string input = cfg_string(s_inputVar, "Default Microphone");
    if (input == "Default Microphone") input.clear();
    return input;
}

float fade_distance() {
    return 4000.0f * static_cast<float>(range_percent()) / 50.0f;
}

float gain_at(float distance) {
    const float edge = fade_distance();
    if (edge <= 0.0f || !std::isfinite(distance) || distance >= edge) return 0.0f;
    const float full = edge * 0.25f;
    if (distance <= full) return 1.0f;
    const float t = 1.0f - (distance - full) / (edge - full);
    return t * t * (3.0f - 2.0f * t);
}

float gain_for(uint8_t slot) {
    daAlink_c* me = daAlink_getAlinkActorClass();
    const char* stage = dComIfGp_getStartStageName();
    if (online()) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        int8_t room = -1;
        if (me == nullptr || !global_slot_position(slot, &x, &y, &z, &room)) return 0.0f;
        const int myRoom = fopAcM_GetRoomNo(me);
        if (room >= 0 && myRoom >= 0 && room != myRoom) return 0.0f;
        const float dx = x - me->current.pos.x, dy = y - me->current.pos.y, dz = z - me->current.pos.z;
        return gain_at(std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    if (!proximity()) return 1.0f;
    const CoopPeer& peer = features_peer_of(slot);
    if (me == nullptr || stage == nullptr || !peer.present || std::strncmp(peer.stage, stage, 8) != 0) return 0.0f;
    const float dx = peer.x - me->current.pos.x, dy = peer.y - me->current.pos.y, dz = peer.z - me->current.pos.z;
    return gain_at(std::sqrt(dx * dx + dy * dy + dz * dz));
}

bool mirrored() {
    static bool s_mirror = false;
    static Clock::time_point s_readAt{};
    const Clock::time_point now = Clock::now();
    if (s_readAt == Clock::time_point{} || now - s_readAt > std::chrono::seconds(5)) {
        s_readAt = now;
        std::string value;
        s_mirror = coop_config_json_value("game.enableMirrorMode", &value) && value == "true";
    }
    return s_mirror;
}

float pan_for(uint8_t slot) {
    const view_class* view = dComIfGd_getView();
    if (view == nullptr) return 0.0f;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (online()) {
        int8_t room = -1;
        if (!global_slot_position(slot, &x, &y, &z, &room)) return 0.0f;
    } else {
        const CoopPeer& peer = features_peer_of(slot);
        if (!peer.present) return 0.0f;
        x = peer.x;
        z = peer.z;
    }
    const cXyz eye = view->lookat.eye;
    const cXyz center = view->lookat.center;
    float fx = center.x - eye.x, fz = center.z - eye.z;
    float dx = x - eye.x, dz = z - eye.z;
    const float flen = std::sqrt(fx * fx + fz * fz), dlen = std::sqrt(dx * dx + dz * dz);
    if (!(flen > 0.001f) || !(dlen > 0.001f)) return 0.0f;
    fx /= flen;
    fz /= flen;

    float pan = (dx * -fz + dz * fx) / dlen;
    const float closeness = std::clamp(dlen / 400.0f, 0.0f, 1.0f);
    pan *= closeness;
    if (mirrored()) pan = -pan;
    return std::clamp(pan, -1.0f, 1.0f);
}

std::string peer_key(uint8_t slot) {
    return (online() ? "online:" : "coop:") + std::to_string(slot);
}

std::string mute_key(uint8_t slot) {
    if (online()) return global_slot_tag(slot);
    const std::string name = features_peer_name(slot);
    return name.empty() ? std::string() : "coop:" + name;
}

void send_frame(const voice_io::Frame& frame) {
    if (frame.opus.empty() || frame.opus.size() > kVoiceMaxOpus) return;
    if (online()) {
        global_send_voice(frame.seq, frame.opus.data(), frame.opus.size(), fade_distance(), kMaxListeners);
    } else {
        coop_voice_send(frame.seq, frame.opus.data(), frame.opus.size());
    }
}

#if defined(_WIN32)
bool key_down(int vk) {
    return vk > 0 && vk < 256 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}
#endif

void update_mute_key() {
#if defined(_WIN32)
    if (!chat_game_in_front()) {
        s_keyWasDown = true;
        return;
    }
    if (s_keyCapture) {

        for (int vk = 8; vk < 255; ++vk) {
            const bool down = key_down(vk);
            if (!down) {
                s_heldAtCapture[vk] = false;
                continue;
            }
            if (s_heldAtCapture[vk]) continue;
            if (vk == VK_ESCAPE) {
                s_keyCapture = false;
                coop_toast("Voice chat", "Mute key unchanged");
                return;
            }

            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;
            svc_config->set_int(mod_ctx, s_muteKeyVar, vk);
            s_keyCapture = false;
            s_keyWasDown = true;
            coop_log::info("coop_mod: [VOICECHAT] mute key {}", vk);
            coop_toast("Voice chat", ("Mute key: " + voice_mute_key_name()).c_str());
            return;
        }
        return;
    }
    const int vk = static_cast<int>(std::clamp<int64_t>(cfg_int(s_muteKeyVar, 'M'), 1, 254));
    const bool down = key_down(vk);
    if (enabled() && down && !s_keyWasDown && !chat_is_typing()) {
        svc_config->set_bool(mod_ctx, s_mutedVar, !mic_muted());
    }
    s_keyWasDown = down;
#endif
}

void sync_device() {
    if (s_inputVar == 0 || s_inputIndexVar == 0) return;
    const std::vector<std::string> devices = voice_input_devices();
    if (devices.empty()) return;
    const int64_t index = cfg_int(s_inputIndexVar, 0);
    const std::string name = cfg_string(s_inputVar, "Default Microphone");
    if (s_lastIndex < 0) {
        s_lastIndex = 0;
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i] == name) s_lastIndex = static_cast<int64_t>(i);
        }
        s_lastName = devices[static_cast<size_t>(s_lastIndex)];
        if (s_lastName != name) svc_config->set_string(mod_ctx, s_inputVar, s_lastName.c_str());
        if (index != s_lastIndex) svc_config->set_int(mod_ctx, s_inputIndexVar, s_lastIndex);
        return;
    }
    if (index != s_lastIndex) {
        const size_t pick = static_cast<size_t>(std::clamp<int64_t>(index, 0, static_cast<int64_t>(devices.size()) - 1));
        s_lastIndex = static_cast<int64_t>(pick);
        s_lastName = devices[pick];
        coop_log::info("coop_mod: [VOICECHAT] microphone '{}'", s_lastName);
        svc_config->set_string(mod_ctx, s_inputVar, s_lastName.c_str());
        if (index != s_lastIndex) svc_config->set_int(mod_ctx, s_inputIndexVar, s_lastIndex);
        return;
    }
    if (name != s_lastName) {
        int64_t found = 0;
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i] == name) found = static_cast<int64_t>(i);
        }
        if (devices[static_cast<size_t>(found)] != name) svc_config->set_string(mod_ctx, s_inputVar, devices[0].c_str());
        s_lastIndex = found;
        s_lastName = devices[static_cast<size_t>(found)];
        svc_config->set_int(mod_ctx, s_inputIndexVar, found);
    }
}

}

void voice_init() {
    if (svc_config == nullptr) return;
    s_enabledVar = reg("voice_enabled", CONFIG_VAR_BOOL, 0, false, nullptr);
    s_proximityVar = reg("voice_proximity", CONFIG_VAR_BOOL, 0, true, nullptr);
    s_rangeVar = reg("voice_range", CONFIG_VAR_INT, 50, false, nullptr);
    s_inputVar = reg("voice_input", CONFIG_VAR_STRING, 0, false, "Default Microphone");
    s_inputIndexVar = reg("voice_input_index", CONFIG_VAR_INT, 0, false, nullptr);
    s_micVolumeVar = reg("voice_mic_volume", CONFIG_VAR_INT, 100, false, nullptr);
    s_playerVolumeVar = reg("voice_player_volume", CONFIG_VAR_INT, 100, false, nullptr);
    s_mutedVar = reg("voice_muted", CONFIG_VAR_BOOL, 0, false, nullptr);
    s_muteKeyVar = reg("voice_mute_key", CONFIG_VAR_INT, 'M', false, nullptr);
    s_voice = std::make_unique<voice_io::Device>();
}

namespace {
Clock::time_point s_hintAt{};
bool s_hintMuted = false;
bool s_hintKnown = false;
bool s_hintLast = false;
const auto kHintHold = std::chrono::milliseconds(1100);
const auto kHintFade = std::chrono::milliseconds(600);

void watch_mute_change() {
    const bool now = mic_muted();
    if (!s_hintKnown) {
        s_hintKnown = true;
        s_hintLast = now;
        return;
    }
    if (now == s_hintLast) return;
    s_hintLast = now;
    if (!enabled() || !voice_can_talk()) return;
    s_hintMuted = now;
    s_hintAt = Clock::now();
}
}

void voice_update() {
    if (!s_voice) return;
    ++s_tick;
    sync_device();
    update_mute_key();
    watch_mute_change();
    const bool on = live();
    s_voice->set(on, !mic_muted(), capture_device(), mic_volume() / 100.0f, player_volume() / 100.0f);
    if (!on) {
        s_status = enabled() ? "On, waiting for a co-op session or Hyrule Online" : "Off";
        s_preroll.clear();
        s_speakers.clear();
        s_talkingUntil = s_localSpeakingUntil = {};
        return;
    }
    if (!s_voice->problem().empty()) {
        s_status = s_voice->problem();
        return;
    }
    s_status = mic_muted() ? "On, microphone muted" : "On";

    const Clock::time_point now = Clock::now();
    for (voice_io::Frame& frame : s_voice->take()) {
        if (frame.loud) {
            s_localSpeakingUntil = now + kSpeakingShown;
            if (s_talkingUntil < now) {
                for (const voice_io::Frame& early : s_preroll) send_frame(early);
            }
            s_talkingUntil = now + kHangover;
            s_preroll.clear();
        }
        if (s_talkingUntil >= now) {
            send_frame(frame);
        } else {
            s_preroll.push_back(std::move(frame));
            while (static_cast<int>(s_preroll.size()) > kPreroll) s_preroll.pop_front();
        }
    }
}

void voice_shutdown() {
    if (s_voice) s_voice->close();
    s_voice.reset();
    s_speakers.clear();
}

void voice_on_frame(uint8_t slot, uint32_t sequence, const uint8_t* opus, size_t size) {
    if (!s_voice || !live() || sequence == 0 || opus == nullptr || size == 0 || size > kVoiceMaxOpus) return;
    if (s_mutedPeers.count(mute_key(slot)) != 0) return;
    const float gain = gain_for(slot);
    const Clock::time_point now = Clock::now();
    Speaker& speaker = s_speakers[slot];
    if (gain <= 0.01f) {
        speaker.shownUntil = {};
        return;
    }
    if (speaker.heardUntil < now) {
        int heard = 0;
        for (const auto& [other, s] : s_speakers) {
            if (other != slot && s.heardUntil >= now) ++heard;
        }
        if (heard >= kMaxHeard) return;
    }
    speaker.heardUntil = now + kHeardFor;
    speaker.shownUntil = now + kSpeakingShown;
    s_voice->play(peer_key(slot), sequence, opus, size, gain, pan_for(slot));
}

void voice_peer_left(uint8_t slot) {
    if (s_voice) {
        s_voice->forget("coop:" + std::to_string(slot));
        s_voice->forget("online:" + std::to_string(slot));
    }
    s_speakers.erase(slot);
}

bool voice_peer_speaking(uint8_t slot) {
    const auto it = s_speakers.find(slot);
    return it != s_speakers.end() && it->second.shownUntil > Clock::now();
}

bool voice_local_speaking() {
    return live() && !mic_muted() && s_localSpeakingUntil > Clock::now();
}

bool voice_peer_muted(uint8_t slot) {
    const std::string key = mute_key(slot);
    return !key.empty() && s_mutedPeers.count(key) != 0;
}

void voice_set_peer_muted(uint8_t slot, bool muted) {
    const std::string key = mute_key(slot);
    if (key.empty()) return;
    if (muted) {
        s_mutedPeers.insert(key);
        voice_peer_left(slot);
    } else {
        s_mutedPeers.erase(key);
    }
}

std::vector<std::string> voice_input_devices() {
    if (s_devices.empty() || s_tick - s_devicesTick > 120) {
        s_devicesTick = s_tick;
        std::vector<std::string> out{"Default Microphone"};
        if (s_voice) {
            for (std::string& name : s_voice->microphones()) {
                if (!name.empty() && name != "Default Microphone") out.push_back(std::move(name));
            }
        }
        s_devices = std::move(out);
    }
    return s_devices;
}

std::string voice_status() { return s_status; }

std::string voice_mute_key_name() {
#if defined(_WIN32)
    const int vk = static_cast<int>(cfg_int(s_muteKeyVar, 'M'));
    LONG lparam = static_cast<LONG>(MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC) << 16);
    if (vk == VK_LEFT || vk == VK_UP || vk == VK_RIGHT || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT ||
        vk == VK_END || vk == VK_HOME || vk == VK_INSERT || vk == VK_DELETE) {
        lparam |= 1 << 24;
    }
    char name[64] = {};
    if (GetKeyNameTextA(lparam, name, static_cast<int>(sizeof(name))) > 0) return name;
    return "Key " + std::to_string(vk);
#else
    return "";
#endif
}

void voice_begin_key_capture() {
    s_keyCapture = true;
    s_keyWasDown = false;
#if defined(_WIN32)
    for (int vk = 0; vk < 256; ++vk) s_heldAtCapture[vk] = key_down(vk);
#endif
}

bool voice_key_capture_active() { return s_keyCapture; }

ConfigVarHandle voice_enabled_var() { return s_enabledVar; }
ConfigVarHandle voice_proximity_var() { return s_proximityVar; }
ConfigVarHandle voice_range_var() { return s_rangeVar; }
ConfigVarHandle voice_input_index_var() { return s_inputIndexVar; }
ConfigVarHandle voice_mic_volume_var() { return s_micVolumeVar; }
ConfigVarHandle voice_player_volume_var() { return s_playerVolumeVar; }
ConfigVarHandle voice_muted_var() { return s_mutedVar; }

bool voice_supported() {
#if defined(COOP_VOICE)
    return true;
#else
    return false;
#endif
}

bool voice_can_talk() {
#if defined(COOP_VOICE_SDL)
    return true;
#else
    return false;
#endif
}

void voice_draw_mute_hint(f32 minX, f32 minY, f32 width, f32 height) {
    if (s_hintAt == Clock::time_point{}) return;
    const auto age = Clock::now() - s_hintAt;
    if (age > kHintHold + kHintFade) {
        s_hintAt = {};
        return;
    }
    f32 a = 1.0f;
    if (age > kHintHold) {
        a = 1.0f - std::chrono::duration<f32>(age - kHintHold).count() /
                       std::chrono::duration<f32>(kHintFade).count();
    }
    const u8 alpha = static_cast<u8>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
    JUTFont* font = mDoExt_getMesgFont();

    const f32 size = height * 0.07f;
    const f32 cell = height * 0.034f;
    const char* word = s_hintMuted ? "Mic off" : "Mic on";
    const f32 textW = font != nullptr ? coop_text_width(font, word, cell) : 0.0f;
    const f32 gap = size * 0.25f;
    const f32 total = size + gap + textW;
    const f32 x = minX + (width - total) * 0.5f;
    const f32 baseline = minY + height * 0.78f;
    coop_draw_voice_icon(x, baseline, size, alpha, s_hintMuted);
    if (font != nullptr) {
        if (J2DGrafContext* port = dComIfGp_getCurrentGrafPort()) port->setup2D();
        const f32 shadow = cell * 0.07f;
        const f32 ty = baseline - size * 0.82f + (size - cell) * 0.5f + cell * 0.85f;
        font->setCharColor(JUtility::TColor(0, 0, 0, static_cast<u8>(alpha * 0.8f)));
        font->drawString_scale(x + size + gap + shadow, ty + shadow, cell, cell, word, true);
        font->setCharColor(s_hintMuted ? JUtility::TColor(255, 120, 110, alpha) : JUtility::TColor(255, 255, 255, alpha));
        font->drawString_scale(x + size + gap, ty, cell, cell, word, true);
    }
    if (J2DGrafContext* port = dComIfGp_getCurrentGrafPort()) port->setup2D();
}
