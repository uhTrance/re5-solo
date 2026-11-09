// The launcher's updater (update.cpp).
#pragma once
#include <windows.h>

#include <string>

struct Release {
    std::wstring tag, name, notes;
    std::wstring url;  // the launcher exe, or the release zip when there is no exe asset
    bool zipped = false;
    unsigned long long size = 0;  // the asset's size in bytes, 0 if not given
    std::string sha256;           // its "digest" (lowercase hex), empty if not given
};

// Asks GitHub in the background; if a newer release is out, posts `message` to `window` with a Release* (lParam),
// which the receiver deletes.
void StartUpdateCheck(HWND window, UINT message);

// Downloads the release's launcher, swaps it in for this exe and starts it with --updated. On success the caller
// closes; on failure `error` says why and this launcher stays as it was.
bool ApplyUpdate(const Release& release, std::wstring& error);

// In a launcher started with --updated: deletes the old exe and the download's leftovers.
void CleanUpAfterUpdate();
