// Airboat (3-1) for one player: holding aim lets go of the wheel (the player steps to the front deck and can
// shoot, the boat coasts); releasing aim goes back to the wheel.
//
// Vanilla swaps seats with a two-player handshake whose last step, exchange(newDriver, newDeck) (0xA8C650),
// guards all but one use of the two players: with no partner it dereferences a null player. So, on the game
// thread (the boat gets a private vtable copy whose update, +0x160, runs ours after the original):
//   - leave: the deck half of exchange() for the player, with nobody driving;
//   - take:  the game's exchange() with the player as driver and a blank dummy as the "deck player"
//            (the one unguarded deck-player access lands in the dummy).
#include <windows.h>

#include "host.h"
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kBoatVtable = 0xFBFFA8;    // uOm1306, the 3-1 airboat
constexpr DWORD kUpdateSlot = 0x160 / 4;   // per-frame update, thiscall, no args
constexpr int kVtableCopy = 0x200;         // dwords copied (well past the class's last slot)

// player fields
constexpr DWORD kParent = 0x18;        // object the player is attached to (the boat while aboard)
constexpr DWORD kState = 0x10E0;       // 0x6A = driving the airboat
constexpr DWORD kIndex = 0x2D9C;       // byte: player slot
constexpr DWORD kBoatFlags = 0x2D8C;   // 0x10 deck, 0x20 wheel
constexpr DWORD kStateDriving = 0x6A;
// boat fields
constexpr DWORD kSeats = 0x18DC;       // bits 4-5 wheel player slot, 6-7 deck player slot

// Short lines: the caption starts bottom left, and a long line runs under the HUD's ammo ring (the game drops
// trailing empty lines, so they cannot lift it). Commas instead of '/', which the font lacks.
constexpr char kControlsHint[] =
    "Drive - Right Stick Up, W Key\n"
    "Aim - RT, R2, Right Mouse Button";

constexpr float kHintSeconds = 30.0f;  // topped up every frame while aboard
constexpr DWORD kOffBoatMs = 1000;
bool g_hintShown;

DWORD g_vtable[kVtableCopy];
BYTE* g_boat;  // the boat given g_vtable, put back by BoatShutdown

// Stand-in "player" for exchange()'s one unguarded access. The boat may keep the pointer, so it lives in the
// loader's memory, which outlives a reload of this DLL.
BYTE* Dummy() { return static_cast<BYTE*>(g_host->memory("boat dummy player", 0x3000)); }

using Update = void(__fastcall*)(void* self, void* edx);
Update g_origUpdate;

// The partner's slot is always empty in solo, so getPlayer() returns null for it: the "no one" seat.
int EmptySlot() { return PartnerSlot(); }

bool AimHeld() {
    DWORD pid;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return false;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) return true;
    if (DInputAimHeld()) return true;  // the aim button on a DirectInput pad (dinput.cpp)
    // Right trigger = aim, on the real pad (xinput.cpp), in whichever XInput slot it is connected.
    for (DWORD user = 0; user < 4; ++user) {
        BYTE state[16] = {};  // XINPUT_STATE: packet, buttons(2), LT(1), RT(1), ...
        if (RawXInputGetState(user, state) == ERROR_SUCCESS && state[7] > 64) return true;
    }
    return false;
}

void SetSeat(BYTE* boat, int shift, int slot) {
    auto& seats = At<BYTE>(boat, kSeats);
    seats = static_cast<BYTE>((seats & ~(3 << shift)) | ((slot & 3) << shift));
}

// The deck half of exchange(nullptr, player), 0xA8C650..0xA8C99F.
void LeaveWheel(BYTE* boat, BYTE* player) {
    At<DWORD>(boat, 0x17FC) |= 0x100;
    SetSeat(boat, 6, At<BYTE>(player, kIndex));
    At<float>(player, 0x30) = 0.0f;  // deck position, boat-local
    At<float>(player, 0x34) = *reinterpret_cast<float*>(0xFBFCD0);
    At<float>(player, 0x38) = *reinterpret_cast<float*>(0xF5472C);
    ThisCall(0x489CE0, player, 0x1249CF0);
    VCall(player, 0x34, boat, 1);  // attach to the boat
    VCall(player, 0xC0);
    At<BYTE>(player, 0x1284) = 1;
    At<BYTE>(player, 0x1114) = 1;
    ThisCall(0xB5C7E0, player, 1);
    At<DWORD>(player, kBoatFlags) |= 0x10;
    At<BYTE*>(player, 0x1B80) = boat;
    VCall(boat, 0xDC, 0, 1, 0, -1, 0);    // boat: idle, no driver
    VCall(player, 0xDC, 0, 1, 0, -1, 0);  // player: normal on-foot action
    At<DWORD>(player, kBoatFlags) = (At<DWORD>(player, kBoatFlags) & ~0x20u) | 0x10u;
    At<BYTE>(player, 0x299D) = 0;
    static const DWORD kAimOrigins[] = {0x29A0, 0x29B0};
    for (DWORD off : kAimOrigins)
        for (int i = 0; i < 3; ++i)
            At<float>(player, off + 4 * i) = reinterpret_cast<float*>(0x1249CF0)[i];
    SetSeat(boat, 4, EmptySlot());
    // Drop the boat-driving camera mode exchange() installed at the wheel, as the boat's helper 0xA8A3A0 does:
    // camera = player vfunc 0xD4; remove(camera+0x2DE0, handle at camera+0x2DD0).
    auto camera = VCall<BYTE*>(player, 0xD4);
    ThisCall(0x843420, camera + 0x2DE0, At<DWORD>(camera, 0x2DD0));
    Log("boat: player left the wheel");
}

void TakeWheel(BYTE* boat, BYTE* player) {
    ThisCall(0xA8C650, boat, player, Dummy());
    SetSeat(boat, 4, At<BYTE>(player, kIndex));
    SetSeat(boat, 6, EmptySlot());
    Log("boat: player took the wheel");
}

void __fastcall BoatUpdate(void* self, void* edx) {
    g_origUpdate(self, edx);
    __try {
        auto boat = static_cast<BYTE*>(self);
        BYTE* player = SoloPlayer();
        // The controls hint stays up while the player is in control (at the wheel, or on the deck outside a
        // cutscene, state bit 0x80000000; the chapter opens aboard in one): its countdown is topped up (KeepHint),
        // and reopened if the game closes it. It closes once out of control for kOffBoatMs, so a brief parent
        // change does not blank it.
        static DWORD offSince, lastOpen;
        DWORD now = GetTickCount();
        bool aboard = player && At<BYTE*>(player, kParent) == boat;
        int slot = aboard ? At<BYTE>(player, kIndex) : -1;
        BYTE seats = At<BYTE>(boat, kSeats);
        bool driving = aboard && At<DWORD>(player, kState) == kStateDriving && ((seats >> 4) & 3) == slot;
        bool onDeck = aboard && ((seats >> 6) & 3) == slot && ((seats >> 4) & 3) == EmptySlot();
        bool inControl = driving || (onDeck && !(At<DWORD>(player, kState) & 0x80000000));
        if (!inControl) {
            if (!offSince) offSince = now;
            if (g_hintShown && now - offSince >= kOffBoatMs) {
                g_hintShown = false;
                CloseHint();
                Log("boat: player not in control of the boat, hint closed");
            }
            if (!aboard) return;
        } else {
            offSince = 0;
            if (!KeepHint(kHintSeconds) && now - lastOpen >= 1000) {
                ShowHint(kControlsHint, kHintSeconds);
                g_hintShown = true;
                lastOpen = now;
            }
        }
        bool aim = AimHeld();
        if (driving && aim)
            LeaveWheel(boat, player);
        else if (onDeck && !aim)
            TakeWheel(boat, player);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("boat: exception in solo seat logic");
    }
}

}  // namespace

// Watcher thread: give the airboat the player is riding a vtable copy with our update.
void BoatTick() {
    BYTE* player = SoloPlayer();
    if (!player) return;
    auto boat = At<BYTE*>(player, kParent);
    if (!boat) return;
    auto vt = At<DWORD*>(boat, 0);
    if (reinterpret_cast<DWORD>(vt) != kBoatVtable) return;  // not the airboat, or already ours
    memcpy(g_vtable, vt, sizeof g_vtable);
    g_origUpdate = reinterpret_cast<Update>(vt[kUpdateSlot]);
    g_vtable[kUpdateSlot] = reinterpret_cast<DWORD>(&BoatUpdate);
    At<DWORD*>(boat, 0) = g_vtable;
    g_boat = boat;
    Log("boat %p: solo seat logic on (update %p)", boat, g_origUpdate);
}

// CoreShutdown: the boat gets its class vtable back (g_vtable lives in this DLL).
void BoatShutdown() {
    __try {
        if (g_boat && At<DWORD*>(g_boat, 0) == g_vtable) At<DWORD>(g_boat, 0) = kBoatVtable;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_boat = nullptr;
}
