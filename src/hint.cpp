// On-screen hints in the game's own message box, e.g. the boat controls.
//
// sMessage ([0x11B26F0]) open (0x7DB3A0, thiscall, ret 0x18): (u8 file, u16 group, u16 index, float time,
// s8 player (-1 = all), int mode). 0x717640 uses it with mode 0 and 3.0 for a timed message. Text: file slot =
// [sMessage+0x84] + file*0x3A0, rMessage = [slot+0x12C] (a pointer, not inline), entries = u32 pointers at
// [rMessage+0x60], count = u16 at [[rMessage+0x64]+0x1C]. An entry is u16 pairs (code, attr): a glyph's attr
// carries its advance width (low byte), {0,0x403} is a new line, {0,0x401} the end. Glyph codes: see Code().
// mes_system (file 0) is loaded in gameplay; its entry 820 reads "(dummy)", so the mod points it at its own
// text (data only).
#include "host.h"
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kMessage = 0x11B26F0, kOpen = 0x7DB3A0;
constexpr DWORD kFiles = 0x84, kFileSize = 0x3A0, kRMessage = 0x12C, kEntries = 0x60, kHeader = 0x64;
constexpr int kSystemFile = 0, kBorrowed = 820;
constexpr DWORD kWindows = 0x2F670, kWindowSize = 0x1C;

WORD g_attr[256];  // attr per glyph code, learned from the game's own text (carries the glyph width)
WORD* g_text;  // in the loader's memory: the game keeps the entry pointer after this DLL unloads

int Code(char c) {
    if (c == ' ') return 0x1C;
    if (c >= '1' && c <= '9') return 0x02 + 2 * (c - '1');  // 0x02 is "1"
    if (c == '0') return -1;                                // code unknown
    if (c >= 'A' && c <= 'Z') return 0x38 + 2 * (c - 'A');
    if (c >= 'a' && c <= 'z') return 0x6C + 2 * (c - 'a');
    switch (c) {
    case '(': return 0x18;
    case ')': return 0x1A;
    case ',': return 0x24;
    case '.': return 0x26;
    case '\'': return 0x2A;
    case '-': return 0x2E;
    // No '/' glyph: 0x32 is a small down-triangle.
    }
    return -1;
}

void LearnWidths(DWORD* entries, int count) {
    for (int i = 0; i < count; ++i) {
        if (entries[i] < 0x10000) continue;
        auto e = reinterpret_cast<WORD*>(entries[i]);
        for (int j = 0; j < 512; j += 2) {
            if (e[j] == 0 && e[j + 1] == 0x401) break;
            if (e[j] && e[j] < 256 && !g_attr[e[j]]) g_attr[e[j]] = e[j + 1];
        }
    }
}

void Encode(const char* text) {
    if (!g_text) g_text = static_cast<WORD*>(g_host->memory("hint text", 1024 * sizeof(WORD)));
    int n = 0;
    for (const char* c = text; *c && n < 1020; ++c) {
        if (*c == '\n') {
            g_text[n++] = 0;
            g_text[n++] = 0x403;
            continue;
        }
        int code = Code(*c);
        if (code < 0 || !g_attr[code]) continue;
        g_text[n++] = static_cast<WORD>(code);
        g_text[n++] = g_attr[code];
    }
    g_text[n++] = 0;
    g_text[n++] = 0x401;
}

// The open window's record: sMessage +0x2F670, two of 0x1C bytes (one per window bit): +0 the player slot it
// shows for (1 playing as Sheva, so it is not the file), +4 entry index, +0xC countdown in seconds (float,
// writable). Only one message window shows at a time and it copies its text when it opens, so re-opening or
// rewriting the entry while one is up changes nothing.
float* HintTimer() {
    auto sm = *reinterpret_cast<BYTE**>(kMessage);
    if (!sm) return nullptr;
    for (int i = 0; i < 2; ++i) {
        BYTE* record = sm + kWindows + i * kWindowSize;
        if (At<DWORD>(record, 4) == kBorrowed && At<float>(record, 0xC) > 0)
            return &At<float>(record, 0xC);
    }
    return nullptr;
}

}  // namespace

// Tops up the hint's countdown; false if it is not on screen.
bool KeepHint(float seconds) {
    float* timer = HintTimer();
    if (timer) *timer = seconds;
    return timer != nullptr;
}

void CloseHint() {
    if (float* timer = HintTimer()) *timer = 0.0f;
}

// Game thread only. `text` may hold '\n'; characters the font table lacks are dropped.
bool ShowHint(const char* text, float seconds, int mode) {
    auto sm = *reinterpret_cast<BYTE**>(kMessage);
    auto files = sm ? At<BYTE*>(sm, kFiles) : nullptr;
    if (!files) return false;
    auto rMessage = At<BYTE*>(files + kSystemFile * kFileSize, kRMessage);  // a pointer in the slot
    if (!rMessage) return false;
    auto entries = At<DWORD*>(rMessage, kEntries);
    auto header = At<BYTE*>(rMessage, kHeader);
    int count = header ? At<WORD>(header, 0x1C) : 0;
    if (!entries || count <= kBorrowed) return false;
    if (!g_attr[0x1C]) LearnWidths(entries, count);
    Encode(text);
    entries[kBorrowed] = reinterpret_cast<DWORD>(g_text);
    int handle = ThisCall<int>(kOpen, sm, kSystemFile, 0, kBorrowed, seconds, -1, mode);
    static int lastMode = -1;  // the handle is not a reliable failure signal: -1 also came back while shown
    if (mode != lastMode) Log("hint: mode %d -> message window %d", mode, handle);
    lastMode = mode;
    return true;
}
