// The partner's lines without the radio filter.
//
// Stage lines with subtitles (s500_40_Sheva etc., sound\bgm\oto\radio_voice) come from the stage's stream list
// (sound\bgm\sNNN_radio.stq, held by sMessage [0x11B26F0] +0x2F6E8). Each line has a request id (its number:
// 40 = s500_40_Sheva) and most of Chris's and Sheva's also a radio recording at id + 100 (140 =
// s500_40_Sheva_r). When a line's speaker is a player slot (0x7D9E0E), sMessage plays it through that player
// (0xB77220: radio only if an AI speaker is far from every human) unless the player is missing
// (0x7E6370), and then plays id + 100. Solo the partner is always missing, so every line of theirs is radio.
//
// Fix (data only): the list is the loaded file, used in place. Header: +8 entry count, +0xC request count,
// +0x20 entry table (0x18 each, +0 name offset), +0x24 request table (0xA4 each: +0 id, +0x26 volume, +0x94
// entry, +0x9C that entry's loaded stream). Each radio request whose entry is "<normal line>_r" gets the
// normal line's entry, stream and volume, and keeps its own 2D playback (the normal request is 3D, placed at
// the speaker, who isn't there). The player's own lines never take the missing-player branch.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kMessage = 0x11B26F0, kStreamList = 0x2F6E8;
constexpr DWORD kImage = 0x60;  // rSoundStreamRequest -> the loaded .stq
constexpr DWORD kMagic = 0x51525453;  // "STRQ"
constexpr DWORD kRequestSize = 0xA4, kVolume = 0x26, kEntry = 0x94, kStream = 0x9C;
constexpr DWORD kRadioOffset = 100;

struct Change { BYTE* request; DWORD entry, stream; WORD volume; DWORD newStream; };
Change g_changes[128];
int g_changeCount;
BYTE* g_list;  // the stream list the changes belong to

const char* EntryName(BYTE* image, DWORD entry) {
    return reinterpret_cast<const char*>(image + At<DWORD>(image + At<DWORD>(image, 0x20) + 0x18 * entry, 0));
}

void Apply(BYTE* list) {
    g_list = list;
    g_changeCount = 0;
    BYTE* image = At<BYTE*>(list, kImage);
    if (!image || At<DWORD>(image, 0) != kMagic) return;
    DWORD entries = At<DWORD>(image, 8), requests = At<DWORD>(image, 0xC);
    BYTE* table = image + At<DWORD>(image, 0x24);
    for (DWORD id = 0; id + kRadioOffset < requests && g_changeCount < 128; ++id) {
        BYTE* normal = table + kRequestSize * id;
        BYTE* radio = table + kRequestSize * (id + kRadioOffset);
        DWORD n = At<DWORD>(normal, kEntry), r = At<DWORD>(radio, kEntry);
        if (n >= entries || r >= entries || !At<DWORD>(normal, kStream)) continue;
        const char* name = EntryName(image, n);
        size_t len = strlen(name);
        if (strncmp(EntryName(image, r), name, len) || strcmp(EntryName(image, r) + len, "_r")) continue;
        g_changes[g_changeCount++] = {radio, r, At<DWORD>(radio, kStream), At<WORD>(radio, kVolume),
                                      At<DWORD>(normal, kStream)};
        At<DWORD>(radio, kEntry) = n;
        At<DWORD>(radio, kStream) = At<DWORD>(normal, kStream);
        At<WORD>(radio, kVolume) = At<WORD>(normal, kVolume);
    }
    Log("voice: %d radio lines in %s now play the normal recording", g_changeCount,
        reinterpret_cast<const char*>(list + 4));
}

}  // namespace

// Watcher thread: once per stream list (a new one comes with each area load).
void VoiceTick() {
    static DWORD lastCheck;
    if (GetTickCount() - lastCheck < 1000) return;
    lastCheck = GetTickCount();
    auto message = *reinterpret_cast<BYTE**>(kMessage);
    BYTE* list = message ? At<BYTE*>(message, kStreamList) : nullptr;
    if (list && list != g_list) Apply(list);
}

// CoreShutdown: put the radio recordings back while that list is still loaded.
void VoiceShutdown() {
    auto message = *reinterpret_cast<BYTE**>(kMessage);
    if (!message || At<BYTE*>(message, kStreamList) != g_list) return;
    __try {
        for (int i = 0; i < g_changeCount; ++i) {
            const Change& c = g_changes[i];
            if (At<DWORD>(c.request, kStream) != c.newStream) continue;
            At<DWORD>(c.request, kEntry) = c.entry;
            At<DWORD>(c.request, kStream) = c.stream;
            At<WORD>(c.request, kVolume) = c.volume;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_changeCount = 0;
}
