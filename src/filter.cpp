// Optional: removes the green/yellow colour filter (re5_solo_nofilter.txt next to the exe).
//
// The tint is uColorCorrectFilter's colour matrix. Its properties (createProperty, vtable +0xC) are an array of
// 16 Corrector objects at +0x30 (mCorrector, 0x30 bytes each: +4 mEnable, +8 mType, +0x10 mFactor/mFactor2),
// tone curves and a gamma. Every frame the area's sLightLinker::ColorCorrect copies its correctors in (0x7C2FB1),
// then the filter's vtable +0x18 (0x482810) folds the enabled ones into the matrix at +0x330, which its draw
// (+0x24) applies. So the filter gets a vtable copy whose +0x18 clears every mEnable first: the matrix stays
// identity and the copy puts the values back for the next frame, so nothing else sees a change.
#include "objhook.h"
#include "solo.h"

namespace {

constexpr DWORD kFilterVtable = 0x10AFF08;  // uColorCorrectFilter
constexpr DWORD kBuildSlot = 0x18 / 4;      // builds the colour matrix from the correctors (thiscall, no args)
constexpr DWORD kCorrectors = 0x30, kCorrectorSize = 0x30, kEnable = 4;
constexpr int kCorrectorCount = 16;

bool NoFilter() {
    static const bool on = [] {
        bool found = MarkerFile("re5_solo_nofilter.txt");
        if (found) Log("filter: colour filter removed (re5_solo_nofilter.txt)");
        return found;
    }();
    return on;
}

void __fastcall Build(BYTE* filter, void*) {
    for (int i = 0; i < kCorrectorCount; ++i) At<BYTE>(filter, kCorrectors + kCorrectorSize * i + kEnable) = 0;
    ThisCall(reinterpret_cast<DWORD*>(kFilterVtable)[kBuildSlot], filter);
}

void PatchFilter(DWORD* copy) { copy[kBuildSlot] = reinterpret_cast<DWORD>(&Build); }

}  // namespace

// Watcher thread: every 100 ms (a new area's filter must be caught before its first frames show the tint), give
// any new colour filter the copy.
void FilterTick() {
    if (!NoFilter()) return;
    static DWORD lastScan;
    if (GetTickCount() - lastScan < 100) return;
    lastScan = GetTickCount();
    ForEachUnit([](BYTE* u) {
        if (At<DWORD>(u, 0) == kFilterVtable) PatchObject(u, kFilterVtable, PatchFilter);
    });
}
