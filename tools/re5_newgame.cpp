// RE5 New Game: resets the Steam RE5 save to an untouched new game with Professional unlocked, in place.
//
// savedata.bin: every 8-byte block is XORed with the previous *encoded* block, the first with a fixed seed.
// Decoded, +0x0 is the Steam ID and +0x8 a checksum (32-bit sum of the dwords at 0x10..0x3BAC and 0x3BB0..0x52AC).
// A new game is all zeros apart from kNewGame (newgame_table.h), so the save is cleared and those values set;
// only the Steam ID is kept (the game rejects a save made under another ID), the date becomes now.
// The old save is copied to "RE5 save backups" next to this exe first.
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "newgame_table.h"

namespace fs = std::filesystem;

namespace {

constexpr unsigned char kSeed[8] = {0x00, 0x21, 0x11, 0x08, 0xC0, 0x4B, 0x00, 0x00};
constexpr size_t kSaveSize = 0x5D50;
constexpr size_t kDate = 0x10;

std::vector<unsigned char> Decode(const std::vector<unsigned char>& enc) {
    std::vector<unsigned char> out(enc.size());
    const unsigned char* key = kSeed;
    for (size_t i = 0; i < enc.size(); i += 8) {
        for (size_t j = 0; j < 8 && i + j < enc.size(); ++j) out[i + j] = enc[i + j] ^ key[j];
        key = &enc[i];
    }
    return out;
}

std::vector<unsigned char> Encode(const std::vector<unsigned char>& raw) {
    std::vector<unsigned char> out(raw.size());
    const unsigned char* key = kSeed;
    for (size_t i = 0; i < raw.size(); i += 8) {
        for (size_t j = 0; j < 8 && i + j < raw.size(); ++j) out[i + j] = raw[i + j] ^ key[j];
        key = &out[i];
    }
    return out;
}

unsigned& Dword(std::vector<unsigned char>& d, size_t off) { return *reinterpret_cast<unsigned*>(&d[off]); }

unsigned Checksum(std::vector<unsigned char>& d) {
    unsigned sum = 0;
    for (size_t o = 0x10; o < 0x10 + 0xEE7 * 4; o += 4) sum += Dword(d, o);
    for (size_t o = 0x3BB0; o < 0x3BB0 + 0x5C0 * 4; o += 4) sum += Dword(d, o);
    return sum;
}

bool GameRunning() {
#ifdef TEST_BUILD  // lets the output be checked while the game is open
    return false;
#endif
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof(pe)};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
        found = _wcsicmp(pe.szExeFile, L"re5dx9.exe") == 0;
    CloseHandle(snap);
    return found;
}

// Steam's install folder from the registry, plus the usual locations.
std::vector<fs::path> SteamRoots() {
    std::vector<fs::path> roots;
    wchar_t buf[MAX_PATH];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buf, &size) ==
        ERROR_SUCCESS)
        roots.emplace_back(buf);
    roots.emplace_back(L"C:\\Program Files (x86)\\Steam");
    roots.emplace_back(L"C:\\Program Files\\Steam");
    return roots;
}

std::vector<fs::path> FindSaves() {
    std::vector<fs::path> saves;
    std::error_code ec;
    for (auto& root : SteamRoots()) {
        fs::path userdata = root / L"userdata";
        if (!fs::is_directory(userdata, ec)) continue;
        for (auto& user : fs::directory_iterator(userdata, ec)) {
            fs::path save = user.path() / L"21690" / L"remote" / L"savedata.bin";
            if (!fs::exists(save, ec)) continue;
            bool dup = false;
            for (auto& s : saves) dup |= fs::equivalent(s, save, ec);
            if (!dup) saves.push_back(save);
        }
    }
    return saves;
}

bool ReadFile(const fs::path& p, std::vector<unsigned char>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

bool WriteFile(const fs::path& p, const std::vector<unsigned char>& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    return bool(f);
}

int Fail(const char* msg) {
    std::printf("\n%s\n", msg);
    return 1;
}

int Run(int argc, wchar_t** argv) {
    std::printf("RE5 New Game: resets your save to an untouched new game with Professional unlocked.\n\n");
    if (GameRunning()) return Fail("Resident Evil 5 is running. Close it and run this again.");

    fs::path save;
    if (argc > 1) {
        save = argv[1];
    } else {
        auto saves = FindSaves();
        if (saves.empty()) return Fail("No RE5 save found (Steam\\userdata\\<id>\\21690\\remote\\savedata.bin).");
        save = saves[0];
        if (saves.size() > 1) {
            for (size_t i = 0; i < saves.size(); ++i) std::printf("  %zu: %ls\n", i + 1, saves[i].c_str());
            std::printf("Which save? ");
            size_t pick = 0;
            int got = std::scanf("%zu", &pick);
            for (int c = 0; c != '\n' && c != EOF;) c = std::getchar();  // rest of the line
            if (got != 1 || pick < 1 || pick > saves.size()) return Fail("Nothing changed.");
            save = saves[pick - 1];
        }
    }
    std::printf("Save: %ls\n", save.c_str());

    std::vector<unsigned char> current;
    if (!ReadFile(save, current)) return Fail("Could not read the save.");
    auto old = Decode(current);
    if (old.size() != kSaveSize) return Fail("This does not look like a Steam RE5 save (wrong size). Nothing changed.");
    if (Dword(old, 8) != Checksum(old)) return Fail("The save's checksum does not match. Nothing changed.");

    std::vector<unsigned char> raw(kSaveSize, 0);
    std::memcpy(raw.data(), old.data(), 8);  // Steam ID
    std::time_t t = std::time(nullptr);
    std::tm now{};
    localtime_s(&now, &t);
    unsigned date[6] = {unsigned(now.tm_year + 1900), unsigned(now.tm_mon + 1), unsigned(now.tm_mday),
                        unsigned(now.tm_hour),        unsigned(now.tm_min),     unsigned(now.tm_sec)};
    std::memcpy(&raw[kDate], date, sizeof(date));
    for (auto& f : kNewGame) Dword(raw, f.offset) = f.value;
    Dword(raw, 8) = Checksum(raw);

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    fs::path backups = fs::path(exe).parent_path() / L"RE5 save backups";
    std::error_code ec;
    fs::create_directories(backups, ec);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H%M%S", &now);
    fs::path backup = backups / (std::string("savedata_") + stamp + ".bin");
    if (!fs::copy_file(save, backup, fs::copy_options::overwrite_existing, ec))
        return Fail("Could not back up the save. Nothing changed.");
    std::printf("Backup: %ls\n", backup.c_str());

    if (!WriteFile(save, Encode(raw))) return Fail("Could not write the save (your backup is safe).");
    std::printf("\nDone: new game with Professional unlocked.\n");
    std::printf("If Steam offers to download a cloud save when you start the game, refuse it.\n");
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    int rc = Run(argc, argv);
    std::printf("\nPress Enter to close.");
    for (int c = 0; c != '\n' && c != EOF;) c = std::getchar();
    return rc;
}
