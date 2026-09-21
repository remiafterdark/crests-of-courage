

#include "mod.hpp"
#include "print.hpp"

#include "mods/service.hpp"
#include "mods/svc/hook.hpp"

#include "JSystem/JAudio2/JASBasicWaveBank.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

const int kWindowFrames = 8;

#pragma pack(push, 1)
struct VoiceEntry {
    uint32_t bank;
    uint32_t waveId;
    uint8_t format;
    uint8_t baseKey;
    uint8_t pad[2];
    float sampleRate;
    uint32_t loopFlag;
    uint32_t loopStart;
    uint32_t loopEnd;
    uint32_t sampleCount;
    int16_t last;
    int16_t penult;
    uint32_t dataSize;

    uint32_t gameSampleCount;
};
#pragma pack(pop)

struct Voice : public JASBasicWaveBank::TWaveHandle {
    void const* getAramBaseAddress() const override { return data.data(); }

    uint32_t waveId = 0;
    VoiceEntry entry{};
    std::vector<uint8_t> data;
};

struct VoiceSet {
    std::string skin;
    std::vector<Voice*> voices;
    JASHeap zeroHeap;
    bool failed = false;
};

std::vector<VoiceSet*> s_sets;

VoiceSet* s_active = nullptr;
int s_framesLeft = 0;

std::string s_wanted;

int s_swapped = 0;

VoiceSet* load_set(const std::string& skin) {
    for (VoiceSet* set : s_sets) {
        if (set->skin == skin) return set->failed ? nullptr : set;
    }
    VoiceSet* set = new VoiceSet();
    set->skin = skin;
    s_sets.push_back(set);

    set->zeroHeap.initRootHeap(nullptr, 0);

    const std::string path = skins_folder_path() + "/" + skin + "/voices.bin";
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        set->failed = true;
        return nullptr;
    }
    char magic[8] = {};
    uint32_t count = 0;
    file.read(magic, sizeof(magic));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (std::memcmp(magic, "COOPVOX3", 8) != 0 || count == 0 || count > 20000) {
        coop_log::warn("coop_mod: [VOICE] '{}' voices.bin is not one of ours", skin);
        set->failed = true;
        return nullptr;
    }

    uint32_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        VoiceEntry entry{};
        if (!file.read(reinterpret_cast<char*>(&entry), sizeof(entry))) break;
        if (entry.dataSize == 0 || entry.dataSize > 1024 * 1024) break;
        Voice* voice = new Voice();
        voice->data.resize(entry.dataSize);
        if (!file.read(reinterpret_cast<char*>(voice->data.data()), entry.dataSize)) {
            delete voice;
            break;
        }
        voice->waveId = entry.waveId;
        voice->entry = entry;
        voice->mWaveInfo.mWaveFormat = entry.format;
        voice->mWaveInfo.mBaseKey = entry.baseKey;
        voice->mWaveInfo.mLoopFlag = entry.loopFlag != 0 ? 0xFF : 0;
        voice->mWaveInfo.mSampleRate = entry.sampleRate;
        voice->mWaveInfo.mOffsetStart = 0;
        voice->mWaveInfo.mOffsetLength = static_cast<int>(entry.dataSize);
        voice->mWaveInfo.mLoopStartSample = entry.loopStart;
        voice->mWaveInfo.mLoopEndSample = static_cast<int>(entry.loopEnd);
        voice->mWaveInfo.mSampleCount = static_cast<int>(entry.sampleCount);
        voice->mWaveInfo.mpLast = entry.last;
        voice->mWaveInfo.mpPenult = entry.penult;
        voice->mHeap = &set->zeroHeap;
        total += entry.dataSize;
        set->voices.push_back(voice);
    }
    if (set->voices.empty()) {
        set->failed = true;
        return nullptr;
    }
    coop_log::info("coop_mod: [VOICE] '{}': all {} sounds loaded ({} KB)", skin,
        set->voices.size(), total / 1024);
    return set;
}

}

DEFINE_HOOK_SYMBOL("?getWaveHandle@JASBasicWaveBank@@UEBAPEAVJASWaveHandle@@I@Z",
    JASWaveHandle*(const JASBasicWaveBank*, u32), VoicesGetWaveHandleHook);
DEFINE_HOOK_SYMBOL("?getWaveHandle@JASSimpleWaveBank@@UEBAPEAVJASWaveHandle@@I@Z",
    JASWaveHandle*(const void*, u32), VoicesGetWaveHandleSimpleHook);

void on_get_wave_handle(ModContext*, void* args, void* retval, void*) {
    if (s_active == nullptr) return;
    JASWaveHandle** result = static_cast<JASWaveHandle**>(retval);

    if (result == nullptr || *result == nullptr) return;
    const u32 waveId = mods::arg<u32>(args, 1);
    for (Voice* voice : s_active->voices) {
        if (voice->waveId != waveId) continue;

        const JASWaveInfo* theirs = (*result)->getWaveInfo();
        if (theirs == nullptr ||
            static_cast<uint32_t>(theirs->mSampleCount) != voice->entry.gameSampleCount) {
            return;
        }
        *result = voice;
        ++s_swapped;
        return;
    }
}

void voices_init() {
    const ModResult basic = mods::hook::add_post<VoicesGetWaveHandleHook>(on_get_wave_handle);
    const ModResult simple = mods::hook::add_post<VoicesGetWaveHandleSimpleHook>(on_get_wave_handle);
    coop_log::info("coop_mod: [VOICE] wave lookup hooks: basic={} simple={}",
        static_cast<int>(basic), static_cast<int>(simple));
}

void voices_begin(const char* skinName) {
    if (skinName == nullptr || skinName[0] == '\0') return;
    for (VoiceSet* set : s_sets) {
        if (set->skin != skinName) continue;
        if (set->failed || set->voices.empty()) return;
        s_active = set;
        s_framesLeft = kWindowFrames;
        return;
    }
    s_wanted = skinName;
}

void voices_begin_local() {

    const std::string mine = skins_local_slot(kSkinChoiceVoice);
    if (!mine.empty()) voices_begin(mine.c_str());
}

void voices_update() {

    if (!s_wanted.empty()) {
        const std::string wanted = s_wanted;
        s_wanted.clear();
        load_set(wanted);
    }
    const std::string mine = skins_local_slot(kSkinChoiceVoice);
    if (!mine.empty()) {
        bool known = false;
        for (VoiceSet* set : s_sets) known = known || set->skin == mine;
        if (!known) load_set(mine);
    }

    if (s_framesLeft <= 0) return;
    if (--s_framesLeft > 0) return;
    s_active = nullptr;
    static int s_reported = -1;
    if (s_swapped != s_reported) {
        s_reported = s_swapped;
        coop_log::info("coop_mod: [VOICE] {} sound(s) played in a player's own voice", s_swapped);
    }
}

void voices_on_disconnected() {
    s_active = nullptr;
    s_framesLeft = 0;
}
