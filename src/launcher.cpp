// RE5 Solo Launcher.exe: installs, updates and uninstalls the mod, toggles its settings and starts the game.
//
// The exe carries both mod files as resources (launcher.rc), so it is the only file to hand out. A foreign
// dinput8.dll (one that doesn't name re5_solo_core.dll) is kept as dinput8.dll.before-re5solo and restored on
// uninstall. A running game holds the loader open: it can't be overwritten or deleted, only renamed
// (dinput8.dll.old*, cleared on the next install or uninstall). The core is never locked (the loader runs a
// copy, re5_solo_core.loadedN.dll) and a running game reloads it within a second.
//
// Install also unpacks the characters' loose scene files (scenefiles.cpp); an install without them is outdated.
// Settings are marker files next to the exe, read when the mod loads. The game folder comes from Steam
// (SteamPath + steamapps\libraryfolders.vdf, or the Linux Steam folders under Wine) or Browse and is
// remembered in HKCU\Software\RE5 Solo. Under Proton, Install also sets the dinput8 override (see below).
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <dwmapi.h>

#include <algorithm>
#include <string>
#include <vector>

bool SceneFilesInstalled(const std::wstring& game);                    // scenefiles.cpp
bool InstallSceneFiles(const std::wstring& game, std::wstring& error);
void RemoveSceneFiles(const std::wstring& game);

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr int kLoaderResource = 101, kCoreResource = 102;
constexpr wchar_t kLoader[] = L"dinput8.dll", kCore[] = L"re5_solo_core.dll";
constexpr wchar_t kBackup[] = L"dinput8.dll.before-re5solo";
constexpr wchar_t kGameExe[] = L"re5dx9.exe";
constexpr wchar_t kGameFolder[] = L"steamapps\\common\\Resident Evil 5";
constexpr wchar_t kRegKey[] = L"Software\\RE5 Solo";
constexpr wchar_t kPlayUrl[] = L"steam://rungameid/21690";

struct Setting { const wchar_t* file; const wchar_t* label; };
constexpr Setting kSettings[] = {
    {L"re5_solo_record.txt", L"Vanilla co-op (the mod steps aside, Sheva's AI comes back)"},
    {L"re5_solo_nofilter.txt", L"Remove the colour filter (no green/yellow tint)"},
    {L"re5_solo_ndesuhalf.txt", L"Ndesu's main bulb at half health (2-3 boss, 13000)"},
    {L"re5_solo_borderless.txt", L"Borderless window (keeps running when you tab out)"},
    {L"re5_solo_120fps.txt", L"120fps fix (no getting stuck on scenery above 60 fps)"},
    {L"re5_solo_debug.txt", L"Debug keys (F9 / F10)"},
    {L"re5_solo_trace.txt", L"Script tracer (writes level-script steps to re5_solo.log)"},
};
constexpr int kSettingCount = sizeof kSettings / sizeof kSettings[0];

enum Id { kIdFolder = 100, kIdBrowse, kIdStatus, kIdInstall, kIdUninstall, kIdPlay, kIdOpen, kIdSetting = 200 };

HWND g_window, g_folder, g_status, g_install, g_uninstall, g_play, g_settings[kSettingCount];
void SetFullScreen(bool full);
std::wstring g_game;  // folder with re5dx9.exe, no trailing backslash

std::wstring Path(const wchar_t* name) { return g_game + L"\\" + name; }
bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool IsGameFolder(const std::wstring& dir) { return !dir.empty() && Exists(dir + L"\\" + kGameExe); }

std::vector<char> ReadAll(const std::wstring& path) {
    std::vector<char> data;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return data;
    LARGE_INTEGER size;
    if (GetFileSizeEx(f, &size) && size.QuadPart < (64 << 20)) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(f, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) || read != data.size())
            data.clear();
    }
    CloseHandle(f);
    return data;
}

std::pair<const char*, DWORD> Resource(int id) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    HGLOBAL g = r ? LoadResource(nullptr, r) : nullptr;
    return {g ? static_cast<const char*>(LockResource(g)) : nullptr, r ? SizeofResource(nullptr, r) : 0};
}

bool SameAsResource(const std::wstring& path, int id) {
    auto [bytes, size] = Resource(id);
    auto file = ReadAll(path);
    return bytes && file.size() == size && memcmp(file.data(), bytes, size) == 0;
}

// Ours = it loads re5_solo_core.dll (any build of the loader).
bool IsOurLoader(const std::wstring& path) {
    auto file = ReadAll(path);
    static const char kMark[] = "re5_solo_core.dll";
    return std::search(file.begin(), file.end(), kMark, kMark + sizeof kMark - 1) != file.end();
}

bool WriteResource(const std::wstring& path, int id) {
    auto [bytes, size] = Resource(id);
    if (!bytes) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, bytes, size, &written, nullptr) && written == size;
    CloseHandle(f);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

// Deletes a file; if it is in use (a running game's loader), renames it out of the way instead.
bool Remove(const std::wstring& path) {
    if (!Exists(path) || DeleteFileW(path.c_str())) return true;
    std::wstring moved = path + L".old" + std::to_wstring(GetTickCount());
    return MoveFileW(path.c_str(), moved.c_str()) != 0;
}

// Leftovers from earlier runs: renamed loaders and the loader's core copies. In-use ones just stay.
void ClearLeftovers() {
    DeleteFileW(Path(L"re5_solo_sheva.txt").c_str());  // a dropped option (forced Sheva); the game's menu picks her
    for (const wchar_t* pattern : {L"dinput8.dll.old*", L"re5_solo_core.loaded*.dll", L"re5_solo_core.dll.old*"}) {
        WIN32_FIND_DATAW find;
        HANDLE h = FindFirstFileW(Path(pattern).c_str(), &find);
        if (h == INVALID_HANDLE_VALUE) continue;
        do DeleteFileW(Path(find.cFileName).c_str());
        while (FindNextFileW(h, &find));
        FindClose(h);
    }
}

bool GameRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W p{sizeof p};
    bool found = false;
    for (BOOL more = Process32FirstW(snap, &p); more && !found; more = Process32NextW(snap, &p))
        found = _wcsicmp(p.szExeFile, kGameExe) == 0;
    CloseHandle(snap);
    return found;
}

// --- Linux (Proton) --------------------------------------------------------------------------------------
//
// On Linux the game is the same Windows exe, run by Proton in its own Wine prefix (steamapps/compatdata/21690/
// pfx), and the launcher runs under Wine too. Wine prefers its built-in dinput8.dll, so the mod needs a DLL
// override ("dinput8"="native,builtin" under HKCU\Software\Wine\DllOverrides) in the game's prefix. If the
// launcher runs in that prefix the registry is set directly; otherwise (e.g. added to Steam as a non-Steam
// game, with a prefix of its own) the game's prefix's user.reg is edited, which needs the game closed.
// Linux paths are reached through Wine's Z: drive.

constexpr char kOverrideSection[] = "[Software\\\\Wine\\\\DllOverrides]";
constexpr char kOverrideLine[] = "\"dinput8\"=\"native,builtin\"";
constexpr wchar_t kOverrideKey[] = L"Software\\Wine\\DllOverrides";

bool IsWine() {
    static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    return wine;
}

std::wstring Widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring wide(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, wide.data(), n);
    return wide;
}

// A Linux path as Wine shows it.
std::wstring FromUnix(const std::string& path) {
    using Fn = LPWSTR(__cdecl*)(LPCSTR);
    static auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_dos_file_name"));
    if (LPWSTR dos = fn ? fn(path.c_str()) : nullptr) {
        std::wstring s = dos;
        HeapFree(GetProcessHeap(), 0, dos);
        return s;
    }
    std::wstring s = L"Z:" + Widen(path);
    std::replace(s.begin(), s.end(), L'/', L'\\');
    return s;
}

std::string Env(const char* name) {
    char buf[1024];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof buf);
    return n && n < sizeof buf ? std::string(buf, n) : std::string();
}

// The game's prefix: steamapps\compatdata\21690\pfx next to the game's steamapps\common.
std::wstring GamePrefix() {
    size_t common = g_game.rfind(L'\\');
    common = common == std::wstring::npos ? common : g_game.rfind(L'\\', common - 1);
    return common == std::wstring::npos ? std::wstring() : g_game.substr(0, common) + L"\\compatdata\\21690\\pfx";
}

// True if this launcher runs in the game's prefix (its C: drive is the game prefix's drive_c).
bool InGamePrefix() {
    using Fn = char*(__cdecl*)(LPCWSTR);
    static auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_unix_file_name"));
    char* unix = fn ? fn(L"C:\\") : nullptr;
    if (!unix) return false;
    std::wstring ours = FromUnix(unix);
    HeapFree(GetProcessHeap(), 0, unix);
    std::wstring theirs = GamePrefix() + L"\\drive_c";
    while (!ours.empty() && ours.back() == L'\\') ours.pop_back();
    return _wcsicmp(ours.c_str(), theirs.c_str()) == 0;
}

// The DllOverrides section of a prefix's user.reg: [begin, end) of its lines, or begin = npos if absent.
std::pair<size_t, size_t> OverrideSection(const std::string& reg) {
    size_t begin = reg.find(std::string("\n") + kOverrideSection);
    if (begin == std::string::npos) return {begin, begin};
    begin = reg.find('\n', begin + 1);
    begin = begin == std::string::npos ? reg.size() : begin + 1;
    size_t end = reg.find("\n[", begin);
    return {begin, end == std::string::npos ? reg.size() : end + 1};
}

size_t OverrideLine(const std::string& reg, std::pair<size_t, size_t> section) {
    if (section.first == std::string::npos) return std::string::npos;
    size_t at = reg.find("\n\"dinput8\"=", section.first - 1);
    return at < section.second ? at + 1 : std::string::npos;
}

bool HasOverride(const std::string& reg) {
    size_t line = OverrideLine(reg, OverrideSection(reg));
    return line != std::string::npos && reg.compare(line + 10, 7, "\"native") == 0;
}

// Adds (on) or removes (off, only our exact value) the override in a user.reg's text. False if unchanged.
bool EditOverride(std::string& reg, bool on) {
    auto section = OverrideSection(reg);
    size_t line = OverrideLine(reg, section);
    size_t lineEnd = line == std::string::npos ? line : reg.find('\n', line);
    lineEnd = lineEnd == std::string::npos ? reg.size() : lineEnd + 1;
    if (!on) {
        if (line == std::string::npos || reg.compare(line, sizeof kOverrideLine - 1, kOverrideLine) != 0) return false;
        reg.erase(line, lineEnd - line);
        return true;
    }
    if (HasOverride(reg)) return false;
    std::string value = std::string(kOverrideLine) + "\n";
    if (line != std::string::npos) {
        reg.replace(line, lineEnd - line, value);
    } else if (section.first != std::string::npos) {
        size_t at = section.first;  // after the key's "#time=" style lines
        while (at < section.second && reg[at] == '#') {
            size_t next = reg.find('\n', at);
            at = next == std::string::npos || next + 1 > section.second ? section.second : next + 1;
        }
        reg.insert(at, value);
    } else {
        reg += std::string(reg.empty() || reg.back() == '\n' ? "" : "\n") + "\n" + kOverrideSection + "\n" + value;
    }
    return true;
}

bool OverrideSet() {
    if (!IsWine()) return true;
    if (InGamePrefix()) {
        wchar_t value[64] = {};
        DWORD size = sizeof value;
        RegGetValueW(HKEY_CURRENT_USER, kOverrideKey, L"dinput8", RRF_RT_REG_SZ, nullptr, value, &size);
        return wcsncmp(value, L"native", 6) == 0;
    }
    auto data = ReadAll(GamePrefix() + L"\\user.reg");
    return HasOverride(std::string(data.begin(), data.end()));
}

bool WriteText(const std::wstring& path, const std::string& text) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
    CloseHandle(f);
    return ok;
}

// Adds (on) or removes (off) the override. Off only removes our exact value.
bool SetOverride(bool on) {
    if (!IsWine()) return true;
    if (InGamePrefix()) {
        if (on) {
            const wchar_t value[] = L"native,builtin";
            return RegSetKeyValueW(HKEY_CURRENT_USER, kOverrideKey, L"dinput8", REG_SZ, value, sizeof value) ==
                   ERROR_SUCCESS;
        }
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kOverrideKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return true;
        RegDeleteValueW(key, L"dinput8");
        RegCloseKey(key);
        return true;
    }
    std::wstring path = GamePrefix() + L"\\user.reg";
    auto data = ReadAll(path);
    if (data.empty()) return !on;  // no prefix yet: the game has never run under Proton
    std::string reg(data.begin(), data.end());
    return !EditOverride(reg, on) || WriteText(path, reg);
}

// Linux Steam installs: the client Proton names, then the usual places (native, and Flatpak).
std::vector<std::wstring> LinuxSteamRoots() {
    std::vector<std::wstring> roots;
    if (!IsWine()) return roots;
    std::string client = Env("STEAM_COMPAT_CLIENT_INSTALL_PATH"), home = Env("HOME");
    if (!client.empty()) roots.push_back(FromUnix(client));
    if (!home.empty())
        for (const char* dir : {"/.steam/steam", "/.local/share/Steam", "/.var/app/com.valvesoftware.Steam/.local/share/Steam"})
            roots.push_back(FromUnix(home + dir));
    return roots;
}

// --- finding the game ------------------------------------------------------------------------------------

std::wstring RegString(HKEY root, const wchar_t* key, const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2];
    DWORD size = sizeof buf;
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    return buf;
}

// A Steam install's libraries: itself, plus each "path" in steamapps\\libraryfolders.vdf (Windows paths with
// doubled backslashes, or Linux paths on Linux).
void AddLibraries(std::wstring steam, std::vector<std::wstring>& libraries) {
    if (steam.empty()) return;
    std::replace(steam.begin(), steam.end(), L'/', L'\\');
    libraries.push_back(steam);
    auto vdf = ReadAll(steam + L"\\steamapps\\libraryfolders.vdf");
    std::string text(vdf.begin(), vdf.end());
    for (size_t at = 0; (at = text.find("\"path\"", at)) != std::string::npos;) {
        size_t open = text.find('"', at + 6), close = open == std::string::npos ? open : text.find('"', open + 1);
        if (close == std::string::npos) break;
        std::string value;
        for (size_t i = open + 1; i < close; ++i) {
            if (text[i] == '\\' && i + 1 < close && text[i + 1] == '\\') ++i;
            value += text[i];
        }
        libraries.push_back(!value.empty() && value[0] == '/' ? FromUnix(value) : Widen(value));
        at = close + 1;
    }
}

std::wstring FindGame() {
    std::wstring saved = RegString(HKEY_CURRENT_USER, kRegKey, L"GameFolder");
    if (IsGameFolder(saved)) return saved;
    std::vector<std::wstring> libraries;
    AddLibraries(RegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"), libraries);
    AddLibraries(RegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"), libraries);
    for (const auto& root : LinuxSteamRoots()) AddLibraries(root, libraries);
    for (const auto& library : libraries) {
        std::wstring dir = library + L"\\" + kGameFolder;
        if (IsGameFolder(dir)) return dir;
    }
    return {};
}

void SetGame(const std::wstring& dir) {
    g_game = dir;
    SetWindowTextW(g_folder, dir.empty() ? L"(not found: use Browse)" : dir.c_str());
    if (!dir.empty())
        RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, L"GameFolder", REG_SZ, dir.c_str(),
                        static_cast<DWORD>((dir.size() + 1) * sizeof(wchar_t)));
}

void Browse() {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"Choose the Resident Evil 5 folder (the one with re5dx9.exe)");
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->Show(g_window)) && SUCCEEDED(dialog->GetResult(&item))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            if (IsGameFolder(path))
                SetGame(path);
            else
                MessageBoxW(g_window, L"That folder has no re5dx9.exe in it.", L"RE5 Solo", MB_ICONWARNING);
            CoTaskMemFree(path);
        }
        item->Release();
    }
    dialog->Release();
}

// --- install / uninstall ---------------------------------------------------------------------------------

enum class State { NoGame, NotInstalled, UpToDate, Outdated };
State g_state = State::NoGame;
bool g_checked[kSettingCount];  // the setting toggles are drawn by us, so they keep no state of their own

State Current() {
    if (!IsGameFolder(g_game)) return State::NoGame;
    std::wstring loader = Path(kLoader);
    if (!Exists(loader) || !IsOurLoader(loader)) return State::NotInstalled;
    return SameAsResource(loader, kLoaderResource) && SameAsResource(Path(kCore), kCoreResource) &&
                   SceneFilesInstalled(g_game) && OverrideSet()
               ? State::UpToDate
               : State::Outdated;
}

void Refresh() {
    State s = Current();
    const wchar_t* text = L"";
    switch (s) {
    case State::NoGame: text = L"Game folder not found. Use Browse to pick the folder with re5dx9.exe."; break;
    case State::NotInstalled:
        text = Exists(Path(kLoader)) ? L"Not installed. Another dinput8.dll is there; it is kept and put back on "
                                       L"uninstall."
                                     : L"Not installed.";
        break;
    case State::UpToDate: text = L"Installed, up to date."; break;
    case State::Outdated:
        text = OverrideSet() ? L"Installed, but a different build. Click Update."
                             : L"Installed, but Proton isn't set to load it yet. Click Update.";
        break;
    }
    SetWindowTextW(g_status, text);
    SetWindowTextW(g_install, s == State::Outdated ? L"Update" : L"Install");
    EnableWindow(g_install, s == State::NotInstalled || s == State::Outdated);
    EnableWindow(g_uninstall, s == State::UpToDate || s == State::Outdated);
    EnableWindow(g_play, s != State::NoGame);
    for (int i = 0; i < kSettingCount; ++i) {
        EnableWindow(g_settings[i], s != State::NoGame);
        g_checked[i] = s != State::NoGame && Exists(Path(kSettings[i].file));
        InvalidateRect(g_settings[i], nullptr, FALSE);
    }
    g_state = s;
    InvalidateRect(g_window, nullptr, FALSE);  // the status dot
}

void Fail(const wchar_t* what) {
    std::wstring text = std::wstring(what) + L"\n\nWindows error " + std::to_wstring(GetLastError()) +
                        L". If the game is in Program Files, run this launcher as administrator.";
    MessageBoxW(g_window, text.c_str(), L"RE5 Solo", MB_ICONERROR);
}

void Install() {
    ClearLeftovers();
    std::wstring loader = Path(kLoader);
    bool running = GameRunning();
    if (Exists(loader) && !IsOurLoader(loader)) {
        if (Exists(Path(kBackup))) {  // never overwrite or delete either foreign dll
            MessageBoxW(g_window, L"Both dinput8.dll and dinput8.dll.before-re5solo are from other mods. Move one "
                                  L"of them out of the game folder, then install again.", L"RE5 Solo", MB_ICONERROR);
            return;
        }
        if (!MoveFileW(loader.c_str(), Path(kBackup).c_str()))
            return Fail(L"Could not set the existing dinput8.dll aside.");
    }
    bool loaderChanged = !SameAsResource(loader, kLoaderResource);
    if (loaderChanged && (!Remove(loader) || !WriteResource(loader, kLoaderResource)))
        return Fail(L"Could not write dinput8.dll.");
    if (!SameAsResource(Path(kCore), kCoreResource) && !WriteResource(Path(kCore), kCoreResource))
        return Fail(L"Could not write re5_solo_core.dll.");
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    std::wstring error;
    if (!InstallSceneFiles(g_game, error)) return Fail((error + L" Sheva and Chris may be missing from scenes.").c_str());
    if (!SetOverride(true)) {
        Refresh();
        MessageBoxW(g_window, L"Installed, but Proton could not be set to load the mod (the game's Proton folder "
                              L"was not found or could not be written; start the game once first). Instead, add "
                              L"this to the game's Steam launch options:\n\nWINEDLLOVERRIDES=\"dinput8=n,b\" %command%",
                    L"RE5 Solo", MB_ICONWARNING);
        return;
    }
    Refresh();
    if (running)
        MessageBoxW(g_window,
                    loaderChanged ? L"Installed. The game is running: restart it to start the mod."
                                  : L"Updated. The running game picks up the new mod within a second.",
                    L"RE5 Solo", MB_ICONINFORMATION);
}

void Uninstall() {
    bool running = GameRunning();
    if (!Remove(Path(kLoader))) return Fail(L"Could not remove dinput8.dll.");
    DeleteFileW(Path(kCore).c_str());
    if (Exists(Path(L"re5_solo_borderless.txt"))) SetFullScreen(true);
    for (const auto& s : kSettings) DeleteFileW(Path(s.file).c_str());
    RemoveSceneFiles(g_game);
    if (Exists(Path(kBackup)) && !Exists(Path(kLoader))) MoveFileW(Path(kBackup).c_str(), Path(kLoader).c_str());
    if (!Exists(Path(kLoader))) SetOverride(false);  // a restored dinput8.dll from another mod still needs it
    ClearLeftovers();
    Refresh();
    MessageBoxW(g_window,
                running ? L"Uninstalled. The game is running: the mod stays active until you close it."
                        : L"Uninstalled. The log (re5_solo.log) and any crash dumps were left in the game folder.",
                L"RE5 Solo", MB_ICONINFORMATION);
}

// The borderless option needs the game in a window: its config.ini (Documents\CAPCOM\RESIDENT EVIL 5) gets
// FullScreen=OFF with it, and ON again without it.
void SetFullScreen(bool full) {
    PWSTR docs = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) return;
    std::wstring ini = std::wstring(docs) + L"\\CAPCOM\\RESIDENT EVIL 5\\config.ini";
    CoTaskMemFree(docs);
    if (Exists(ini)) WritePrivateProfileStringW(L"DISPLAY", L"FullScreen", full ? L"ON" : L"OFF", ini.c_str());
}

void ToggleSetting(int i) {
    std::wstring path = Path(kSettings[i].file);
    bool on = !g_checked[i];
    if (!wcscmp(kSettings[i].file, L"re5_solo_borderless.txt")) SetFullScreen(!on);
    if (on) {
        HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) Fail(L"Could not save the setting.");
        else CloseHandle(f);
    } else if (!DeleteFileW(path.c_str()) && Exists(path)) {
        Fail(L"Could not save the setting.");
    }
    Refresh();
}

// --- window ----------------------------------------------------------------------------------------------
//
// The look is the "slate" theme of the LiveSplit web layout, as in the RE5 Save Manager: soft dark cards on a
// blue-black page, one quiet blue accent (Install and Play get the current-split bar: an accent tint with a solid
// left edge). Every control is drawn by us; the title bar takes the page colour (Windows 11).

// Slate (livesplit-web-layout themes.css): page #0e1014, cards rgb(24 27 33), text #e8eaed at 100%/75%,
// hairlines white 8%, accent #7aa2f7.
constexpr COLORREF kBg = RGB(14, 16, 20), kCard = RGB(24, 27, 33), kLine = RGB(42, 45, 51);
constexpr COLORREF kText = RGB(232, 234, 237), kMuted = RGB(178, 180, 184), kDim = RGB(98, 102, 110);
constexpr COLORREF kAccent = RGB(122, 162, 247), kAccentBar = RGB(40, 49, 67), kAccentBarHover = RGB(48, 60, 84);
constexpr COLORREF kHover = RGB(32, 36, 43), kField = RGB(18, 20, 25);
constexpr COLORREF kDanger = RGB(240, 118, 124);   // Uninstall
constexpr COLORREF kOk = RGB(158, 206, 106), kWarn = RGB(229, 192, 123);  // status dot

constexpr wchar_t kTitle[] = L"RE5 Solo Launcher";
int g_dpi = 96;
int S(int px) { return MulDiv(px, g_dpi, 96); }

HFONT g_titleFont, g_labelFont, g_buttonFont, g_smallFont;
HBRUSH g_cardBrush, g_fieldBrush;
HWND g_hot;  // control under the mouse
std::vector<RECT> g_cards;  // card rectangles in 96-dpi units (scaled when painted)
RECT g_folderBox, g_header;
int g_statusY, g_optionsLabelY, g_folderLabelY;

RECT Scaled(const RECT& r) { return RECT{S(r.left), S(r.top), S(r.right), S(r.bottom)}; }

// Segoe UI Variable Text where Windows has it (the layout's font), else Segoe UI.
const wchar_t* FontFace() {
    static const wchar_t* face = [] {
        LOGFONTW lf{};
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Segoe UI Variable Text");
        bool found = false;
        HDC dc = GetDC(nullptr);
        EnumFontFamiliesExW(dc, &lf, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int {
            *reinterpret_cast<bool*>(p) = true;
            return 0;
        }, reinterpret_cast<LPARAM>(&found), 0);
        ReleaseDC(nullptr, dc);
        return found ? L"Segoe UI Variable Text" : L"Segoe UI";
    }();
    return face;
}

HFONT Font(int pt, int weight) {
    return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                       FontFace());
}

void Fill(HDC dc, const RECT& r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

// A slate card: rounded, flat, no border unless one is asked for.
void Card(HDC dc, const RECT& r, COLORREF fill, COLORREF edge = CLR_INVALID, int radius = 10) {
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH)), oldPen = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, fill);
    SetDCPenColor(dc, edge == CLR_INVALID ? fill : edge);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, S(radius), S(radius));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

void Text(HDC dc, const wchar_t* s, RECT r, HFONT font, COLORREF color, UINT format) {
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, s, -1, &r, format | DT_NOPREFIX);
    SelectObject(dc, old);
}

bool IsPrimary(int id) { return id == kIdInstall || id == kIdPlay; }
bool IsToggle(int id) { return id >= kIdSetting && id < kIdSetting + kSettingCount; }
bool OnPage(int id) { return id == kIdOpen || id == kIdPlay; }  // the bottom row; everything else sits on a card

// Owner-drawn controls are painted into a memory bitmap first (no flicker).
void DrawControl(const DRAWITEMSTRUCT& item) {
    RECT r = item.rcItem;
    int w = r.right - r.left, h = r.bottom - r.top;
    HDC dc = CreateCompatibleDC(item.hDC);
    HBITMAP bmp = CreateCompatibleBitmap(item.hDC, w, h), oldBmp = static_cast<HBITMAP>(SelectObject(dc, bmp));
    int id = static_cast<int>(item.CtlID);
    RECT box{0, 0, w, h};
    Fill(dc, box, OnPage(id) ? kBg : kCard);  // corners show what is behind
    bool disabled = item.itemState & ODS_DISABLED, pressed = item.itemState & ODS_SELECTED;
    bool hot = item.hwndItem == g_hot && !disabled, focus = item.itemState & ODS_FOCUS;
    wchar_t label[128];
    GetWindowTextW(item.hwndItem, label, 128);
    if (IsToggle(id)) {  // a row like the layout's info rows: label left, a small switch right
        bool on = g_checked[id - kIdSetting];
        if (hot || focus) Card(dc, box, hot ? kHover : kCard, focus ? kLine : CLR_INVALID, 8);
        int th = S(18), tw = S(34), ty = (h - th) / 2, tx = w - S(10) - tw;
        COLORREF track = disabled ? kField : on ? (hot ? kAccentBarHover : kAccentBar) : kLine;
        COLORREF edge = disabled ? kLine : on ? kAccent : (hot ? kDim : kLine);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH)), oldPen = SelectObject(dc, GetStockObject(DC_PEN));
        SetDCBrushColor(dc, track);
        SetDCPenColor(dc, edge);
        RoundRect(dc, tx, ty, tx + tw, ty + th, th, th);
        int knob = th - S(8), kx = on ? tx + tw - S(4) - knob : tx + S(4);
        COLORREF knobColor = disabled ? kDim : on ? kAccent : kMuted;
        SetDCBrushColor(dc, knobColor);
        SetDCPenColor(dc, knobColor);
        Ellipse(dc, kx, ty + S(4), kx + knob, ty + S(4) + knob);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        RECT tr{S(10), 0, tx - S(10), h};
        Text(dc, label, tr, g_labelFont, disabled ? kDim : on ? kText : kMuted,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {  // buttons are cards; Install and Play get the current-split bar
        bool primary = IsPrimary(id) && !disabled;
        COLORREF fill = primary ? (hot ? kAccentBarHover : kAccentBar) : (hot || !OnPage(id) ? kHover : kCard);
        if (pressed) fill = primary ? kAccentBar : kLine;
        if (disabled) fill = OnPage(id) ? kCard : kField;
        Card(dc, box, fill, hot && !primary ? kLine : CLR_INVALID, 8);
        if (primary) {  // 3px accent edge, inset like LiveSplit's
            RECT e{0, S(4), S(3), h - S(4)};
            Fill(dc, e, kAccent);
        }
        if (focus) Card(dc, RECT{S(3), S(3), w - S(3), h - S(3)}, fill, kMuted, 6);
        COLORREF color = disabled ? kDim : id == kIdUninstall ? kDanger : kText;
        Text(dc, label, box, primary ? g_buttonFont : g_labelFont, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    BitBlt(item.hDC, r.left, r.top, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

void Paint(HDC dc, const RECT& client) {
    Fill(dc, client, kBg);
    for (const RECT& c : g_cards) Card(dc, Scaled(c), kCard);
    // Header, like LiveSplit's game and category lines.
    RECT h = Scaled(g_header);
    Text(dc, L"Resident Evil 5", RECT{h.left, h.top + S(9), h.right, h.top + S(31)}, g_titleFont, kText,
         DT_CENTER | DT_SINGLELINE);
    Text(dc, L"Solo Launcher", RECT{h.left, h.top + S(30), h.right, h.bottom - S(6)}, g_smallFont, kMuted,
         DT_CENTER | DT_SINGLELINE);
    // Section labels, inside their cards.
    int m = S(26);
    Text(dc, L"Game folder", RECT{m, S(g_folderLabelY), client.right - m, S(g_folderLabelY + 18)}, g_smallFont, kMuted,
         DT_LEFT | DT_SINGLELINE);
    Text(dc, L"Options", RECT{m, S(g_optionsLabelY), client.right - m, S(g_optionsLabelY + 18)}, g_smallFont, kMuted,
         DT_LEFT | DT_SINGLELINE);
    // The folder field's box (the edit control inside it has no border).
    Card(dc, Scaled(g_folderBox), kField, kLine, 8);
    // Status dot.
    COLORREF dot = g_state == State::UpToDate ? kOk : g_state == State::Outdated ? kWarn : kDim;
    int cx = m + S(5), cy = S(g_statusY + 10);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH)), oldPen = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, dot);
    SetDCPenColor(dc, dot);
    Ellipse(dc, cx - S(4), cy - S(4), cx + S(4), cy + S(4));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

// Hover tracking for owner-drawn buttons, plus Enter: IsDialogMessage only clicks push buttons on Enter, and
// owner-drawn ones don't report as push buttons (the dialog manager would restyle them), so take it here.
LRESULT CALLBACK HoverProc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_GETDLGCODE && lp && reinterpret_cast<const MSG*>(lp)->message == WM_KEYDOWN &&
        reinterpret_cast<const MSG*>(lp)->wParam == VK_RETURN)
        return DefSubclassProc(w, msg, wp, lp) | DLGC_WANTMESSAGE;
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        if (IsWindowEnabled(w))
            SendMessageW(GetParent(w), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(w), BN_CLICKED), reinterpret_cast<LPARAM>(w));
        return 0;
    }
    if (msg == WM_MOUSEMOVE && g_hot != w) {
        HWND old = g_hot;
        g_hot = w;
        TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, w, 0};
        TrackMouseEvent(&tme);
        if (old) InvalidateRect(old, nullptr, FALSE);
        InvalidateRect(w, nullptr, FALSE);
    } else if (msg == WM_MOUSELEAVE && g_hot == w) {
        g_hot = nullptr;
        InvalidateRect(w, nullptr, FALSE);
    }
    return DefSubclassProc(w, msg, wp, lp);
}

HWND Add(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g_window,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (style & BS_OWNERDRAW) SetWindowSubclass(c, HoverProc, 1, 0);
    return c;
}

HWND AddButton(const wchar_t* text, int x, int y, int w, int h, int id) {
    return Add(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, x, y, w, h, id, g_labelFont);
}

void Build() {
    constexpr int W = 540, M = 12, P = 14;  // window width, page margin, card padding
    constexpr int L = M + P, R = W - M - P;  // card content edges
    g_header = RECT{M, M, W - M, 66};
    g_cards.push_back(g_header);
    // Game card: folder, status, install.
    int top = 74, y = top + 12;
    g_folderLabelY = y;
    y += 22;
    g_folderBox = RECT{L, y, R - 104, y + 34};
    g_folder = Add(L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL, L + 10, y + 8, R - 104 - L - 20, 20, kIdFolder,
                   g_labelFont);
    AddButton(L"Browse", R - 96, y, 96, 34, kIdBrowse);
    y += 34 + 12;
    g_statusY = y;
    g_status = Add(L"STATIC", L"", SS_NOPREFIX, L + 16, y + 1, R - L - 16, 36, kIdStatus, g_labelFont);
    y += 40;
    g_install = AddButton(L"Install", L, y, 132, 36, kIdInstall);
    g_uninstall = AddButton(L"Uninstall", L + 140, y, 132, 36, kIdUninstall);
    y += 36 + P;
    g_cards.push_back(RECT{M, top, W - M, y});
    // Options card.
    top = y + 8, y = top + 12;
    g_optionsLabelY = y;
    y += 24;
    for (int i = 0; i < kSettingCount; ++i)
        g_settings[i] = Add(L"BUTTON", kSettings[i].label, BS_OWNERDRAW | WS_TABSTOP, L - 6, y + 32 * i, R - L + 12, 30,
                            kIdSetting + i, g_labelFont);
    y += 32 * kSettingCount + 6;
    Add(L"STATIC", L"Options take effect the next time the game starts.", SS_NOPREFIX, L + 4, y, R - L - 4, 18, 0,
        g_smallFont);
    y += 18 + P;
    g_cards.push_back(RECT{M, top, W - M, y});
    // Bottom row, on the page.
    y += 12;
    AddButton(L"Open game folder", M, y, 170, 42, kIdOpen);
    g_play = AddButton(L"Play", W - M - 170, y, 170, 42, kIdPlay);
    RECT r{0, 0, S(W), S(y + 42 + M)};
    AdjustWindowRect(&r, GetWindowLongW(g_window, GWL_STYLE), FALSE);
    SetWindowPos(g_window, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
}

LRESULT CALLBACK WindowProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == kIdBrowse) { Browse(); Refresh(); }
        else if (id == kIdInstall) Install();
        else if (id == kIdUninstall) {
            if (MessageBoxW(w, L"Remove the mod, its settings and the unpacked scene files from the game folder?",
                            L"RE5 Solo", MB_YESNO | MB_ICONQUESTION) == IDYES)
                Uninstall();
        } else if (id == kIdPlay) {
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(w, L"open", kPlayUrl, nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                MessageBoxW(w, L"Steam could not be opened from here. Start Resident Evil 5 from Steam.", L"RE5 Solo",
                            MB_ICONINFORMATION);
        }
        else if (id == kIdOpen && !g_game.empty()) ShellExecuteW(w, L"open", g_game.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        else if (IsToggle(id) && (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == BN_DOUBLECLICKED))
            ToggleSetting(id - kIdSetting);
        return 0;
    }
    case WM_DRAWITEM: DrawControl(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT client;
        GetClientRect(w, &client);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, client.right, client.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        Paint(mem, client);
        BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && g_status) Refresh();  // the folder may have changed meanwhile
        break;
    case WM_CTLCOLORSTATIC: {  // labels on the cards, and the read-only folder field
        HDC dc = reinterpret_cast<HDC>(wp);
        bool field = reinterpret_cast<HWND>(lp) == g_folder;
        SetTextColor(dc, field || reinterpret_cast<HWND>(lp) == g_status ? kText : kMuted);
        SetBkColor(dc, field ? kField : kCard);
        return reinterpret_cast<LRESULT>(field ? g_fieldBrush : g_cardBrush);
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    HDC screen = GetDC(nullptr);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    g_titleFont = Font(12, FW_SEMIBOLD);
    g_labelFont = Font(10, FW_NORMAL);
    g_buttonFont = Font(10, FW_SEMIBOLD);
    g_smallFont = Font(9, FW_NORMAL);
    g_cardBrush = CreateSolidBrush(kCard);
    g_fieldBrush = CreateSolidBrush(kField);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"RE5SoloLauncher";
    RegisterClassW(&wc);
    g_window = CreateWindowExW(0, wc.lpszClassName, kTitle, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                               CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, nullptr, nullptr, instance, nullptr);
    BOOL dark = TRUE;  // a dark title bar (DWMWA_USE_IMMERSIVE_DARK_MODE) in the page colour (DWMWA_CAPTION_COLOR)
    DwmSetWindowAttribute(g_window, 20, &dark, sizeof dark);
    COLORREF caption = kBg;
    DwmSetWindowAttribute(g_window, 35, &caption, sizeof caption);
    Build();
    SetGame(FindGame());
    Refresh();
    ShowWindow(g_window, show);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(g_window, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
