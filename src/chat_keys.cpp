

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <chrono>
#include <cstdint>
#include <string>

#if defined(__linux__) && !defined(__ANDROID__)
#define COOP_SDL_KEYS 1

extern "C" {
const bool* SDL_GetKeyboardState(int* numkeys);
uint16_t SDL_GetModState(void);
uint32_t SDL_GetKeyFromScancode(int scancode, uint16_t modstate, bool key_event);
void* SDL_GetKeyboardFocus(void);
}
#endif

struct ChatKeys {
    std::string typed;
    int backspaces = 0;
    bool enter = false;
    bool escape = false;
    bool paste = false;
};

namespace {
using Clock = std::chrono::steady_clock;

struct BackRepeat {
    bool held = false;
    Clock::time_point since;
    Clock::time_point last;

    int step(bool down, bool pressed) {
        const Clock::time_point now = Clock::now();
        if (pressed) {
            held = true;
            since = last = now;
            return 1;
        }
        if (!down) {
            held = false;
            return 0;
        }
        if (held && now - since > std::chrono::milliseconds(400) && now - last > std::chrono::milliseconds(45)) {
            last = now;
            return 1;
        }
        return 0;
    }
};
BackRepeat s_back;

#if defined(_WIN32)
bool s_was[256] = {};

bool ours_in_front() {
    HWND front = GetForegroundWindow();
    DWORD pid = 0;
    if (front != nullptr) GetWindowThreadProcessId(front, &pid);
    return pid == GetCurrentProcessId();
}

bool down(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}
#endif

#if COOP_SDL_KEYS

const int kScanT = 23;
const int kScanV = 25;
const int kScanReturn = 40;
const int kScanEscape = 41;
const int kScanBackspace = 42;
const int kScanKeypadEnter = 88;
const int kScanLeftCtrl = 224;
const int kScanRightGui = 231;
const uint16_t kModShift = 0x0003;
const uint16_t kModCtrl = 0x00C0;
const uint16_t kModAlt = 0x0300;
const uint16_t kModCaps = 0x2000;
const int kScanCount = 512;
bool s_was[kScanCount] = {};

bool ours_in_front() {
    return SDL_GetKeyboardFocus() != nullptr;
}

bool key_down(const bool* state, int count, int scan) {
    return state != nullptr && scan < count && state[scan];
}
#endif
}

bool chat_key_pressed() {
#if defined(_WIN32) || COOP_SDL_KEYS
    static bool s_wasT = false;
#if defined(_WIN32)
    const bool t = ours_in_front() && down('T');
#else
    int count = 0;
    const bool* state = SDL_GetKeyboardState(&count);
    const bool t = ours_in_front() && key_down(state, count, kScanT);
#endif
    const bool pressed = t && !s_wasT;
    s_wasT = t;
    return pressed;
#else
    return false;
#endif
}

bool chat_keys_supported() {
#if defined(_WIN32) || COOP_SDL_KEYS
    return true;
#else
    return false;
#endif
}

void chat_keys_begin() {
    s_back = BackRepeat{};
#if defined(_WIN32)
    for (int vk = 0; vk < 256; ++vk) s_was[vk] = down(vk);
#elif COOP_SDL_KEYS
    int count = 0;
    const bool* state = SDL_GetKeyboardState(&count);
    for (int i = 0; i < kScanCount; ++i) s_was[i] = key_down(state, count, i);
#endif
}

void chat_keys_poll(ChatKeys& out) {
#if defined(_WIN32)
    if (!ours_in_front()) {
        chat_keys_begin();
        return;
    }
    BYTE state[256] = {};
    const bool shift = down(VK_SHIFT);
    const bool ctrl = down(VK_CONTROL);
    const bool alt = down(VK_MENU);
    if (shift) state[VK_SHIFT] = 0x80;
    if (ctrl) state[VK_CONTROL] = 0x80;
    if (alt) state[VK_MENU] = 0x80;
    if ((GetKeyState(VK_CAPITAL) & 1) != 0) state[VK_CAPITAL] = 0x01;
    for (int vk = 8; vk < 256; ++vk) {
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LSHIFT ||
            vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU ||
            vk == VK_RMENU || vk == VK_CAPITAL) {
            continue;
        }
        const bool d = down(vk);
        const bool pressed = d && !s_was[vk];
        s_was[vk] = d;
        if (vk == VK_BACK) {
            out.backspaces += s_back.step(d, pressed);
            continue;
        }
        if (!pressed) continue;
        if (vk == VK_RETURN) {
            out.enter = true;
            continue;
        }
        if (vk == VK_ESCAPE) {
            out.escape = true;
            continue;
        }
        if (ctrl && !alt) {
            if (vk == 'V') out.paste = true;
            continue;
        }
        wchar_t buf[8] = {};
        const UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
        const int n = ToUnicode(static_cast<UINT>(vk), scan, state, buf, 8, 0);
        for (int i = 0; i < n; ++i) {
            if (buf[i] >= 0x20 && buf[i] < 0x7F) out.typed.push_back(static_cast<char>(buf[i]));
        }
    }
#elif COOP_SDL_KEYS
    if (!ours_in_front()) {
        chat_keys_begin();
        return;
    }
    int count = 0;
    const bool* state = SDL_GetKeyboardState(&count);
    if (state == nullptr) return;
    const uint16_t mods = SDL_GetModState();
    const bool ctrl = (mods & kModCtrl) != 0;
    const bool alt = (mods & kModAlt) != 0;

    const uint16_t typeMods = static_cast<uint16_t>(mods & (kModShift | kModCaps));
    for (int scan = 4; scan < kScanCount; ++scan) {
        if (scan >= kScanLeftCtrl && scan <= kScanRightGui) continue;
        const bool d = key_down(state, count, scan);
        const bool pressed = d && !s_was[scan];
        s_was[scan] = d;
        if (scan == kScanBackspace) {
            out.backspaces += s_back.step(d, pressed);
            continue;
        }
        if (!pressed) continue;
        if (scan == kScanReturn || scan == kScanKeypadEnter) {
            out.enter = true;
            continue;
        }
        if (scan == kScanEscape) {
            out.escape = true;
            continue;
        }
        if (ctrl && !alt) {
            if (scan == kScanV) out.paste = true;
            continue;
        }
        const uint32_t key = SDL_GetKeyFromScancode(scan, typeMods, false);
        if (key >= 0x20 && key < 0x7F) out.typed.push_back(static_cast<char>(key));
    }
#else
    (void)out;
#endif
}
