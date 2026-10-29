// Lever lifts (uOm0944, a uOmSwitch whose object is the lift itself; 3-3), lever platforms (uOm090d_Floor)
// and jack platforms (uOm0924_Container) for one player: stepping on does what the partner did in co-op.
//
// Lever lift: in co-op one player pulls the lever (a few metres away) while the other rides. The pull is the
// lever's event handler (vtable +0x188, 0xB19D70; event 0, param 1): it marks the slot pressed
// (+0x1985[slot]), toggles +0x17FC (1 = up, 0 = down) and starts action 3 with that player. Writing +0x17FC
// alone does nothing. The lift's position (+0x30) is the platform's spot (it moves by animation); a player
// riding measured 60 units from it. Trigger: the player steps into that spot (an edge, so finishing a
// ride doesn't send it straight back) while the lever is idle (+0x10E0 == 0).
#include "objhook.h"
#include "solo.h"

namespace {

// Per-object state for the hooked objects below. Entries are made (or reset, for a new object at a reused
// address) by the watcher just before it hooks the object, so the game thread only ever looks them up.
template <class T, int N = 8>
struct PerObject {
    struct Entry { BYTE* object; T state; };
    Entry entries[N];
    int next;

    T* Find(BYTE* object) {
        for (auto& e : entries)
            if (e.object == object) return &e.state;
        return nullptr;
    }
    void Reset(BYTE* object, const T& state) {
        Entry* e = nullptr;
        for (auto& x : entries)
            if (x.object == object) e = &x;
        if (!e) e = &entries[next++ % N];
        *e = {object, state};
    }
};

constexpr DWORD kLiftVtable = 0x100AC98;  // uOm0944
constexpr DWORD kState = 0x10E0, kUpDown = 0x17FC;
constexpr DWORD kPressed = 0x1985, kPrevPressed = 0x1989;  // BYTE[4] per slot
constexpr DWORD kSetReady = 0xA82C50, kNextUpDown = 0xB36F60;
constexpr DWORD kSetAction = 0xDC;
constexpr float kRadius = 130.0f;
constexpr DWORD kPlatformVtable = 0x100B0C8;  // uOm090d_Floor
constexpr float kPlatformRadius = 200.0f;

bool OnLift(BYTE* lift, BYTE* player) {
    float dx = At<float>(lift, 0x30) - At<float>(player, 0x30);
    float dz = At<float>(lift, 0x38) - At<float>(player, 0x38);
    return dx * dx + dz * dz < kRadius * kRadius;
}

// The pull handler's steps, but with no player for action 3: given one, the action's start (0xB3A760) puts
// that player into the lever-pulling action, which teleports them off the lift to the lever.
void PullWithoutPlayer(BYTE* lift) {
    int slot = SoloSlot();
    At<BYTE>(lift, kPrevPressed + slot) = At<BYTE>(lift, kPressed + slot);
    At<BYTE>(lift, kPressed + slot) = 1;
    ThisCall(kSetReady, lift, slot);
    At<DWORD>(lift, kUpDown) = ThisCall<int>(kNextUpDown, lift) == 1 ? 1 : 0;
    VCall(lift, kSetAction, 3, 0, static_cast<void*>(nullptr), -1, 0);
}

struct LiftState { bool seen, wasOn; };
PerObject<LiftState> g_lifts;

void AfterLiftUpdate(BYTE* lift) {
    LiftState* self = g_lifts.Find(lift);
    if (!self) return;
    BYTE* player = SoloPlayer();
    bool on = player && At<BYTE*>(player, 0x10F8) && OnLift(lift, player);
    bool stepped = self->seen && on && !self->wasOn;  // the first frame only records where the player is
    self->seen = true;
    self->wasOn = on;
    if (!stepped || At<DWORD>(lift, kState) != 0) return;
    PullWithoutPlayer(lift);
    Log("lift %p: player stepped on, lever pulled (now %s)", lift, At<DWORD>(lift, kUpDown) ? "up" : "down");
}

// Lever platforms (3-3 uOm090d_Floor): in co-op a single lever (uOm08bc) sends the platform across and back,
// one way per pull, through the level script; nothing links the lever and the platform. The platform's
// position is a blend (+0x1988, 0..1) of two fixed end points, driven by its own actions. Each action's start
// (0xB1BA30) copies +0x17FC..+0x1808 (blend, direction +1/-1, speed, top speed) into +0x1988..+0x1994;
// action 1 speeds up in that direction to the top speed, action 4 is stopped. The script's trips are
// {start, +-1, 0, 0.1}, so the mod starts the same trip when the player steps on and stops it at the far end
// (pulling the lever with no player stalls it in its action 3).
constexpr DWORD kParams = 0x17FC, kBlend = 0x1988;
constexpr int kSpeedUp = 1, kStopped = 4;

struct Trip { bool seen, wasOn, moving; float dir; DWORD offSince; };
PerObject<Trip> g_trips;

void AfterPlatformUpdate(BYTE* platform) {
    BYTE* player = SoloPlayer();
    bool on = false;
    if (player && At<BYTE*>(player, 0x10F8)) {
        float dx = At<float>(platform, 0x30) - At<float>(player, 0x30);
        float dy = At<float>(player, 0x34) - At<float>(platform, 0x34);
        float dz = At<float>(platform, 0x38) - At<float>(player, 0x38);
        on = dx * dx + dz * dz < kPlatformRadius * kPlatformRadius && dy > -50 && dy < 150;
    }
    Trip* found = g_trips.Find(platform);
    if (!found) return;
    Trip& trip = *found;
    if (!trip.seen) {  // the first frame only records where the player is
        trip = {true, on, false, 0, on ? 0 : GetTickCount()};
        return;
    }
    float blend = At<float>(platform, kBlend);
    if (trip.moving) {
        if (At<int>(platform, kState) != kSpeedUp) {
            trip.moving = false;  // the script took over
        } else if ((trip.dir > 0 && blend >= 1.0f) || (trip.dir < 0 && blend <= 0.0f)) {
            trip.moving = false;
            float* params = &At<float>(platform, kParams);  // action 4's start copies these in too: the far end,
            params[0] = trip.dir > 0 ? 1.0f : 0.0f;          // or it jumps back to the start
            params[2] = 0.0f;
            VCall(platform, kSetAction, kStopped, 0, static_cast<void*>(nullptr), -1, 0);
            Log("platform %p: arrived", platform);
        }
    }
    // Re-arm only once the player has been off for a second: the player doesn't read as "on" while riding,
    // so on arrival they would count as stepping on again and send the platform straight back.
    if (on || trip.moving) trip.offSince = 0;
    else if (!trip.offSince) trip.offSince = GetTickCount();
    if (trip.offSince && GetTickCount() - trip.offSince >= 1000) trip.wasOn = false;
    bool stepped = on && !trip.wasOn;
    if (on) trip.wasOn = true;
    int state = At<int>(platform, kState);
    if (!stepped || trip.moving || (state != 0 && state != kStopped)) return;
    trip.dir = blend < 0.5f ? 1.0f : -1.0f;
    float* params = &At<float>(platform, kParams);
    params[0] = blend;
    params[1] = trip.dir;
    params[2] = 0.0f;
    params[3] = 0.1f;
    trip.moving = true;
    VCall(platform, kSetAction, kSpeedUp, 0, static_cast<void*>(nullptr), -1, 0);
    Log("platform %p: player stepped on, moving %s", platform, trip.dir > 0 ? "out" : "back");
}

// Jack platforms (5-x area 500 uOm0924_Container): in co-op one player shoots the platform the other stands
// on, which lifts it (action 1 down -> 2 rising -> 0 up, +0x10E0); you can't shoot the one under your feet.
// So the shots are swapped: shooting a down platform while the player stands on another down one puts the shot
// one back down and starts the player's. Writing +0x10E0 = 2 alone does nothing; the action call is needed.
constexpr DWORD kJackVtable = 0x100C010;  // uOm0924_Container
constexpr int kJackDown = 1, kJackRising = 2;
constexpr float kJackRadius = 350.0f;

struct Jack { int lastState; };
PerObject<Jack> g_jacks;

// The player on top of the container (its +0x30 is the base; a player on it measured 250 above it, 210 from
// its centre). Once it moves the player is parented to it (+0x18) and their +0x30 becomes local to it.
bool OnJack(BYTE* jack, BYTE* player) {
    if (At<BYTE*>(player, 0x18) == jack) return true;
    float dx = At<float>(jack, 0x30) - At<float>(player, 0x30);
    float dy = At<float>(player, 0x34) - At<float>(jack, 0x34);
    float dz = At<float>(jack, 0x38) - At<float>(player, 0x38);
    return dx * dx + dz * dz < kJackRadius * kJackRadius && dy > 150 && dy < 400;
}

void AfterJackUpdate(BYTE* jack) {
    Jack* self = g_jacks.Find(jack);
    if (!self) return;
    int state = At<int>(jack, kState);
    bool shot = self->lastState == kJackDown && state == kJackRising;
    self->lastState = state;
    BYTE* player = SoloPlayer();
    if (!shot || !player || !At<BYTE*>(player, 0x10F8) || OnJack(jack, player)) return;
    for (auto& other : g_jacks.entries) {
        BYTE* o = other.object;
        if (!o || o == jack || !IsHooked(o, kJackVtable)) continue;  // stale after a reload
        if (At<int>(o, kState) != kJackDown || !OnJack(o, player)) continue;
        VCall(jack, kSetAction, kJackDown, 0, static_cast<void*>(nullptr), -1, 0);
        VCall(o, kSetAction, kJackRising, 0, static_cast<void*>(nullptr), -1, 0);
        self->lastState = kJackDown;
        other.state.lastState = kJackRising;
        Log("jack %p shot while the player stands on jack %p: raising that one instead", jack, o);
        return;
    }
}

}  // namespace

// Watcher thread: once a second, hook any lever lift, lever platform or jack platform on the unit lists.
void LiftTick() {
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 1000) return;
    lastScan = GetTickCount();
    ForEachUnit([&](BYTE* u) {
        DWORD vt = At<DWORD>(u, 0);
        if (vt == kLiftVtable) {
            g_lifts.Reset(u, {});
            HookObject(u, kLiftVtable, AfterLiftUpdate);
        } else if (vt == kPlatformVtable) {
            g_trips.Reset(u, {});
            HookObject(u, kPlatformVtable, AfterPlatformUpdate);
        } else if (vt == kJackVtable) {
            g_jacks.Reset(u, {At<int>(u, kState)});  // all known, so the one the player stands on can be found
            HookObject(u, kJackVtable, AfterJackUpdate);
        }
    });
}
