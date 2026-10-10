

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#if defined(__linux__) && !defined(__ANDROID__)
#define COOP_SDL_KEYS 1

typedef bool (*SdlEventFilter)(void* userdata, void* event);
extern "C" {
const bool* SDL_GetKeyboardState(int* numkeys);
uint16_t SDL_GetModState(void);
uint32_t SDL_GetKeyFromScancode(int scancode, uint16_t modstate, bool key_event);
void* SDL_GetKeyboardFocus(void);
void SDL_SetEventFilter(SdlEventFilter filter, void* userdata);
bool SDL_GetEventFilter(SdlEventFilter* filter, void** userdata);
}
#endif

struct ChatKeys {
    std::string typed;
    int backspaces = 0;
    bool enter = false;
    bool escape = false;
    bool paste = false;
    int scroll = 0;
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
const int kScanPageUp = 75;
const int kScanPageDown = 78;
const int kScanDown = 81;
const int kScanUp = 82;
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

std::atomic<bool> s_blocking{false};

#if defined(_WIN32)
HWND s_blockWnd = nullptr;
WNDPROC s_blockPrev = nullptr;

LRESULT CALLBACK block_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {

    if (s_blocking && (msg == WM_KEYDOWN || msg == WM_CHAR || msg == WM_DEADCHAR)) return 0;
    return CallWindowProcW(s_blockPrev, hwnd, msg, wp, lp);
}

bool block_install() {
    if (s_blockWnd != nullptr) return true;
    HWND front = GetForegroundWindow();
    if (front == nullptr || !ours_in_front()) return false;
    const LONG_PTR prev = SetWindowLongPtrW(front, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&block_proc));
    if (prev == 0) return false;
    s_blockWnd = front;
    s_blockPrev = reinterpret_cast<WNDPROC>(prev);
    return true;
}

void block_remove() {
    if (s_blockWnd == nullptr) return;
    if (GetWindowLongPtrW(s_blockWnd, GWLP_WNDPROC) != reinterpret_cast<LONG_PTR>(&block_proc)) return;
    SetWindowLongPtrW(s_blockWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_blockPrev));
    s_blockWnd = nullptr;
    s_blockPrev = nullptr;
}
#elif COOP_SDL_KEYS

const uint32_t kEventKeyDown = 0x300;
const uint32_t kEventTextEditing = 0x302;
const uint32_t kEventTextInput = 0x303;
bool s_filterSet = false;
SdlEventFilter s_prevFilter = nullptr;
void* s_prevData = nullptr;

bool block_filter(void*, void* event) {
    if (s_blocking && event != nullptr) {
        const uint32_t type = *static_cast<const uint32_t*>(event);
        if (type == kEventKeyDown || type == kEventTextEditing || type == kEventTextInput) return false;
    }
    return s_prevFilter == nullptr || s_prevFilter(s_prevData, event);
}

bool block_install() {
    if (s_filterSet) return true;
    s_prevFilter = nullptr;
    s_prevData = nullptr;
    SDL_GetEventFilter(&s_prevFilter, &s_prevData);
    SDL_SetEventFilter(block_filter, nullptr);
    s_filterSet = true;
    return true;
}

void block_remove() {
    if (!s_filterSet) return;
    SdlEventFilter now = nullptr;
    void* data = nullptr;
    SDL_GetEventFilter(&now, &data);
    if (now != block_filter) return;
    SDL_SetEventFilter(s_prevFilter, s_prevData);
    s_filterSet = false;
}
#endif
}

bool chat_keys_block(bool on) {
#if defined(_WIN32) || COOP_SDL_KEYS
    if (on) {
        const bool installed = block_install();
        s_blocking = installed;
        return installed;
    }
    s_blocking = false;
    block_remove();
    return true;
#else
    (void)on;
    return false;
#endif
}

bool chat_game_in_front() {
#if defined(_WIN32) || COOP_SDL_KEYS
    return ours_in_front();
#else
    return true;
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
        if (vk == VK_PRIOR || vk == VK_NEXT || vk == VK_UP || vk == VK_DOWN) {
            out.scroll += vk == VK_PRIOR ? 4 : vk == VK_NEXT ? -4 : vk == VK_UP ? 1 : -1;
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
        if (scan == kScanPageUp || scan == kScanPageDown || scan == kScanUp || scan == kScanDown) {
            out.scroll += scan == kScanPageUp ? 4 : scan == kScanPageDown ? -4 : scan == kScanUp ? 1 : -1;
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
