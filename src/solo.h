#pragma once

// Shared by the core's files (and Log/CrashLogInit by the loader).
void Log(const char* fmt, ...);
void CrashLogInit();                // loader: crashlog.cpp
bool MarkerFile(const char* name);  // core.cpp: re5_solo_*.txt next to the exe
bool RecordMode();                  // record.cpp: vanilla co-op, the mod only logs

// Start and stop (core.cpp calls these).
extern volatile bool g_stopping;  // the core is shutting down: its threads exit
void SoloInit();
void SoloShutdown();
void CageInit();
void CageShutdown();
void ScriptShutdown();
void DebugShutdown();
void BoatShutdown();
void CollapseShutdown();
void VoiceShutdown();
void RestoreHookedObjects();  // objhook.cpp

// Watcher thread, every tick (solo.cpp).
void ShevaTick();
void RecordTick();
void BoatTick();
void GapTick();
void SplitTick();
void ElevatorTick();
void TwistTick();
void CollapseTick();
void PulleyTick();
void VoiceTick();
void CrankTick();
void BoxTick();
void CrocTick();
void LeverTick();
void LiftTick();
void JeepTick();
void NdesuTick();
void EnemyHpTick();
void WeskerTick();
void DebugTick();
void ScriptTick();
void ZoneTick();
void FilterTick();
void UroborosTick();
void BackupTick();
void InventoryTick();
void PauseTick();
void PlayerTick();  // snag.cpp: every player's vtable copy, the 120fps fix (also in record mode)

void DeathAfterMove(BYTE* player);  // death.cpp, after each player's move (snag.cpp's hook)

void SplitFrame(BYTE* player);      // split.cpp, every frame from gap.cpp's player-manager hook
void LocalJoinFrame();              // localjoin.cpp, likewise
extern volatile DWORD g_boxGoTick;  // box.cpp: when a co-op box last went

// Pads (the loader's padhook.cpp calls the filters).
void XInputFilter(DWORD user, void* state);
void DInputFilter(void* device, DWORD size, void* data);
bool DInputAimHeld();
DWORD RawXInputGetState(DWORD user, void* state);

// The game's message window (hint.cpp).
bool ShowHint(const char* text, float seconds, int mode = 0);
bool KeepHint(float seconds);
void CloseHint();
