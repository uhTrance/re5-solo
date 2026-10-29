// Save backups. The game keeps one save (Steam's userdata\<account>\21690\remote\savedata.bin) and rewrites it
// at every chapter start and checkpoint, so one bad save loses things for good (loading a chapter as the other
// character saved the absent partner's inventory as empty). Each new version of the save is copied to
// re5_solo_saves\ next to the exe, named after the save's own write time, keeping the newest kKeep.
#include <windows.h>
#include <cstdio>
#include <cstring>

#include "solo.h"

namespace {

constexpr int kKeep = 30;

// <steam>\steamapps\common\Resident Evil 5\re5dx9.exe -> <steam>, and the exe's folder.
bool Paths(char* steam, char* game) {
    GetModuleFileNameA(nullptr, game, MAX_PATH);
    char* slash = strrchr(game, '\\');
    if (!slash) return false;
    *slash = 0;
    strcpy(steam, game);
    for (int up = 0; up < 3; ++up) {
        slash = strrchr(steam, '\\');
        if (!slash) return false;
        *slash = 0;
    }
    return true;
}

// The signed-in account's save (HKCU\Software\Valve\Steam\ActiveProcess ActiveUser), else the first one found.
bool FindSave(char* save) {
    char steam[MAX_PATH], game[MAX_PATH];
    if (!Paths(steam, game)) return false;
    DWORD user = 0, size = sizeof user;
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam\\ActiveProcess", "ActiveUser", RRF_RT_REG_DWORD,
                     nullptr, &user, &size) == ERROR_SUCCESS && user) {
        snprintf(save, MAX_PATH, "%s\\userdata\\%lu\\21690\\remote\\savedata.bin", steam, user);
        if (GetFileAttributesA(save) != INVALID_FILE_ATTRIBUTES) return true;
    }
    char pattern[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s\\userdata\\*", steam);
    WIN32_FIND_DATAA f;
    HANDLE h = FindFirstFileA(pattern, &f);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        if (!(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || f.cFileName[0] == '.') continue;
        snprintf(save, MAX_PATH, "%s\\userdata\\%s\\21690\\remote\\savedata.bin", steam, f.cFileName);
        found = GetFileAttributesA(save) != INVALID_FILE_ATTRIBUTES;
    } while (!found && FindNextFileA(h, &f));
    FindClose(h);
    return found;
}

// Oldest first by name (the names are timestamps); deletes all but the newest kKeep.
void Prune(const char* dir) {
    char pattern[MAX_PATH], names[256][64];
    snprintf(pattern, MAX_PATH, "%s\\savedata_*.bin", dir);
    WIN32_FIND_DATAA f;
    HANDLE h = FindFirstFileA(pattern, &f);
    if (h == INVALID_HANDLE_VALUE) return;
    int n = 0;
    do {
        if (n < 256 && strlen(f.cFileName) < 64) strcpy(names[n++], f.cFileName);
    } while (FindNextFileA(h, &f));
    FindClose(h);
    for (int i = 1; i < n; ++i)  // insertion sort, n is small
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; --j) {
            char t[64];
            strcpy(t, names[j]), strcpy(names[j], names[j - 1]), strcpy(names[j - 1], t);
        }
    for (int i = 0; i < n - kKeep; ++i) {
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s\\%s", dir, names[i]);
        DeleteFileA(path);
    }
}

}  // namespace

// Watcher thread: every 2 s; a changed save is copied once it has not been written for 2 s.
void BackupTick() {
    static DWORD lastCheck;
    if (GetTickCount() - lastCheck < 2000) return;
    lastCheck = GetTickCount();
    static char save[MAX_PATH];
    static bool looked, found;
    if (!looked) {
        looked = true;
        found = FindSave(save);
        Log(found ? "backup: watching %s" : "backup: no save found%s", found ? save : "");
    }
    if (!found) return;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(save, GetFileExInfoStandard, &a)) return;
    static FILETIME seen;  // the write time last looked at: copy only when it has held for one check
    bool settled = CompareFileTime(&a.ftLastWriteTime, &seen) == 0;
    seen = a.ftLastWriteTime;
    if (!settled) return;
    FILETIME local;
    SYSTEMTIME t;
    FileTimeToLocalFileTime(&a.ftLastWriteTime, &local);
    FileTimeToSystemTime(&local, &t);
    char steam[MAX_PATH], dir[MAX_PATH], copy[MAX_PATH];
    if (!Paths(steam, dir)) return;
    strcat(dir, "\\re5_solo_saves");
    CreateDirectoryA(dir, nullptr);
    snprintf(copy, MAX_PATH, "%s\\savedata_%04d%02d%02d_%02d%02d%02d.bin", dir, t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond);
    if (GetFileAttributesA(copy) != INVALID_FILE_ATTRIBUTES) return;  // this version is already backed up
    if (!CopyFileA(save, copy, TRUE)) {
        Log("backup: copy to %s failed (%lu)", copy, GetLastError());
        return;
    }
    Log("backup: saved %s", strrchr(copy, '\\') + 1);
    Prune(dir);
}
