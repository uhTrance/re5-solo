// The launcher's updater. On start a thread asks GitHub for the newest release of uhTrance/re5-solo; if its tag is
// newer than RE5SOLO_VERSION the window is told, asks, and on a yes the new launcher is downloaded (a release's
// "*Launcher*.exe" asset, else its .zip, unpacked with Windows' tar), put in place of this exe (a running exe can be
// renamed, not overwritten) and started with --updated, which installs its mod files and deletes the old exe.
//
// RE5SOLO_UPDATE_URL (environment) replaces the GitHub API address, for testing against a local server.
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <string>
#include <vector>

#include "update.h"
#include "version.h"

namespace {

constexpr wchar_t kApi[] = L"https://api.github.com/repos/uhTrance/re5-solo/releases/latest";
constexpr wchar_t kAgent[] = L"RE5-Solo-Launcher/" RE5SOLO_VERSION;

bool Get(const std::wstring& url, std::string& body, DWORD maxBytes) {
    URL_COMPONENTSW parts{sizeof parts};
    wchar_t host[256], path[2048];
    parts.lpszHostName = host, parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path, parts.dwUrlPathLength = 2048;
    wchar_t extra[2048];
    parts.lpszExtraInfo = extra, parts.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return false;
    std::wstring object = std::wstring(path) + extra;
    bool ok = false;
    HINTERNET session = WinHttpOpen(kAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!session) session = WinHttpOpen(kAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    HINTERNET connect = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", object.c_str(), nullptr, nullptr, nullptr, flags)
                                : nullptr;
    if (request) {
        WinHttpSetTimeouts(request, 10000, 10000, 15000, 60000);
        const wchar_t* headers = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
        DWORD status = 0, size = sizeof status;
        if (WinHttpSendRequest(request, headers, static_cast<DWORD>(-1), nullptr, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr) &&
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size,
                                nullptr) &&
            status == 200) {
            body.clear();
            ok = true;
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available)) { ok = false; break; }
                if (!available) break;
                size_t at = body.size();
                if (at + available > maxBytes) { ok = false; break; }
                body.resize(at + available);
                DWORD read = 0;
                if (!WinHttpReadData(request, body.data() + at, available, &read)) { ok = false; break; }
                body.resize(at + read);
            }
        }
        WinHttpCloseHandle(request);
    }
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

// The string value after "key": at or after `from` (JSON escapes \/ and \" handled; enough for GitHub's fields).
std::string Field(const std::string& json, const char* key, size_t from = 0, size_t* end = nullptr) {
    std::string tag = std::string("\"") + key + "\"";
    size_t k = json.find(tag, from);
    if (k == std::string::npos) return {};
    size_t q = json.find('"', json.find(':', k + tag.size()) + 1);
    if (q == std::string::npos) return {};
    std::string out;
    size_t i = q + 1;
    for (; i < json.size() && json[i] != '"'; ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            char c = json[++i];
            out += c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c;
        } else {
            out += json[i];
        }
    }
    if (end) *end = i;
    return out;
}

// The number after "key": in json[from, to) (the last one there), or 0.
unsigned long long Number(const std::string& json, const char* key, size_t from, size_t to) {
    std::string tag = std::string("\"") + key + "\":";
    size_t k = json.rfind(tag, to);
    if (k == std::string::npos || k < from) return 0;
    return strtoull(json.c_str() + k + tag.size(), nullptr, 10);
}

std::string Sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32];
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                           static_cast<ULONG>(data.size()), 0) == 0 &&
            BCryptFinishHash(hash, digest, sizeof digest, 0) == 0) {
            static const char kHex[] = "0123456789abcdef";
            for (unsigned char b : digest) hex += kHex[b >> 4], hex += kHex[b & 15];
        }
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return hex;
}

std::wstring Wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// "v1.2.3" / "1.2.3" -> {1, 2, 3}
std::vector<int> Parts(const std::wstring& v) {
    std::vector<int> out;
    int n = -1;
    for (wchar_t c : v) {
        if (c >= L'0' && c <= L'9') n = (n < 0 ? 0 : n * 10) + (c - L'0');
        else if (c == L'.' && n >= 0) out.push_back(n), n = -1;
        else if (c == L'-' || c == L'+') break;  // pre-release / build suffix
    }
    if (n >= 0) out.push_back(n);
    while (out.size() < 3) out.push_back(0);
    return out;
}

bool Newer(const std::wstring& tag, const std::wstring& mine) { return Parts(tag) > Parts(mine); }

std::wstring ApiUrl() {
    wchar_t buf[1024];
    DWORD n = GetEnvironmentVariableW(L"RE5SOLO_UPDATE_URL", buf, 1024);
    return n && n < 1024 ? buf : kApi;
}

struct CheckArgs {
    HWND window;
    UINT message;
};

DWORD WINAPI Check(LPVOID p) {
    auto args = static_cast<CheckArgs*>(p);
    std::string json;
    if (Get(ApiUrl(), json, 4 << 20)) {
        auto release = new Release;
        release->tag = Wide(Field(json, "tag_name"));
        release->name = Wide(Field(json, "name"));
        release->notes = Wide(Field(json, "body"));
        // Assets: the first launcher exe, else the first zip.
        // GitHub lists an asset's "size" and "digest" before its "browser_download_url".
        std::wstring exe, zip;
        unsigned long long exeSize = 0, zipSize = 0;
        std::string exeDigest, zipDigest;
        size_t at = 0, end = 0;
        for (;;) {
            size_t assetFrom = at;
            std::string url = Field(json, "browser_download_url", at, &end);
            if (url.empty()) break;
            size_t urlAt = json.rfind("\"browser_download_url\"", end);
            unsigned long long size = Number(json, "size", assetFrom, urlAt);
            std::string digest;
            size_t d = json.rfind("\"digest\"", urlAt);
            if (d != std::string::npos && d >= assetFrom) {
                digest = Field(json, "digest", d);
                digest = digest.rfind("sha256:", 0) == 0 ? digest.substr(7) : std::string();
                for (auto& c : digest) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            }
            at = end;
            std::wstring w = Wide(url);
            auto lower = w;
            for (auto& c : lower) c = towlower(c);
            bool isExe = lower.size() > 4 && lower.compare(lower.size() - 4, 4, L".exe") == 0 &&
                         lower.find(L"launcher") != std::wstring::npos;
            bool isZip = lower.size() > 4 && lower.compare(lower.size() - 4, 4, L".zip") == 0;
            if (isExe && exe.empty()) exe = w, exeSize = size, exeDigest = digest;
            if (isZip && zip.empty()) zip = w, zipSize = size, zipDigest = digest;
        }
        release->zipped = exe.empty();
        release->url = release->zipped ? zip : exe;
        release->size = release->zipped ? zipSize : exeSize;
        release->sha256 = release->zipped ? zipDigest : exeDigest;
        bool post = !release->tag.empty() && !release->url.empty() && Newer(release->tag, RE5SOLO_VERSION);
        if (!post || !PostMessageW(args->window, args->message, 0, reinterpret_cast<LPARAM>(release))) delete release;
    }
    delete args;
    return 0;
}

std::wstring ExePath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

bool WriteAll(const std::wstring& path, const std::string& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    CloseHandle(f);
    return ok;
}

// Runs tar (Windows 10 1803+) to unpack a zip into dir; waits for it.
bool Unzip(const std::wstring& zip, const std::wstring& dir) {
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(system) + L"\\tar.exe\" -xf \"" + zip + L"\" -C \"" + dir + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    DWORD code = 1;
    if (WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);  // stuck: not left running
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

// The launcher exe somewhere under dir (the zip may hold a folder).
std::wstring FindLauncher(const std::wstring& dir) {
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &d);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    do {
        std::wstring name = d.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring full = dir + L"\\" + name;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) found = FindLauncher(full);
        else if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0 &&
                 (StrStrIW(name.c_str(), L"launcher") != nullptr))
            found = full;
    } while (found.empty() && FindNextFileW(h, &d));
    FindClose(h);
    return found;
}

bool IsExe(const std::string& data) { return data.size() > 0x1000 && data[0] == 'M' && data[1] == 'Z'; }

// The download folder, removed (a failed earlier attempt could leave an old launcher in it).
void RemoveWork(const std::wstring& work) {
    if (GetFileAttributesW(work.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    SHFILEOPSTRUCTW op{};
    std::wstring from = work + L'\0';
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}

}  // namespace

void StartUpdateCheck(HWND window, UINT message) {
    if (GetEnvironmentVariableW(L"RE5SOLO_NO_UPDATE", nullptr, 0)) return;
    auto args = new CheckArgs{window, message};
    HANDLE t = CreateThread(nullptr, 0, Check, args, 0, nullptr);
    if (t) CloseHandle(t);
    else delete args;
}

bool ApplyUpdate(const Release& release, std::wstring& error) {
    // Only over https (a test address from RE5SOLO_UPDATE_URL may be plain http).
    bool testing = GetEnvironmentVariableW(L"RE5SOLO_UPDATE_URL", nullptr, 0) != 0;
    if (!testing && _wcsnicmp(release.url.c_str(), L"https://", 8) != 0) {
        error = L"The update's address isn't secure (https), so it wasn't downloaded.";
        return false;
    }
    std::string data;
    if (!Get(release.url, data, 64 << 20)) {
        error = L"The download failed. Check your connection, or download it from GitHub.";
        return false;
    }
    // Checked against what the release lists, so a cut-off or damaged download is never put in place.
    if ((release.size && data.size() != release.size) ||
        (!release.sha256.empty() && Sha256Hex(data) != release.sha256)) {
        error = L"The download was incomplete or damaged. Try again, or download it from GitHub.";
        return false;
    }
    std::wstring exe = ExePath();
    std::wstring fresh = exe + L".new";
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring work = std::wstring(temp) + L"re5solo_update";
    if (release.zipped) {
        RemoveWork(work);
        CreateDirectoryW(work.c_str(), nullptr);
        std::wstring zip = work + L"\\release.zip";
        std::wstring unpacked = work + L"\\files";
        CreateDirectoryW(unpacked.c_str(), nullptr);
        if (!WriteAll(zip, data) || !Unzip(zip, unpacked)) {
            error = L"The downloaded release could not be unpacked.";
            return false;
        }
        std::wstring found = FindLauncher(unpacked);
        if (found.empty() || !CopyFileW(found.c_str(), fresh.c_str(), FALSE)) {
            error = L"The release has no launcher in it.";
            return false;
        }
    } else {
        if (!IsExe(data) || !WriteAll(fresh, data)) {
            error = IsExe(data) ? L"Could not save the new launcher next to this one." : L"The download was not a program.";
            DeleteFileW(fresh.c_str());
            return false;
        }
    }
    std::wstring old = exe + L".old";
    DeleteFileW(old.c_str());
    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        error = L"Could not replace this launcher (is its folder read-only?).";
        DeleteFileW(fresh.c_str());
        return false;
    }
    if (!MoveFileExW(fresh.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);  // put this one back
        error = L"Could not put the new launcher in place.";
        return false;
    }
    std::wstring cmd = L"\"" + exe + L"\" --updated";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        // Put this launcher back, so what is in place is known to run.
        if (MoveFileExW(exe.c_str(), fresh.c_str(), MOVEFILE_REPLACE_EXISTING) &&
            MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(fresh.c_str());
            error = L"The new launcher could not be started, so this one was kept.";
        } else {
            error = L"The new launcher is in place but could not be started. Open it again.";
        }
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void CleanUpAfterUpdate() {
    std::wstring old = ExePath() + L".old";
    for (int i = 0; i < 20 && GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES; ++i) {
        if (DeleteFileW(old.c_str())) break;
        Sleep(250);  // the old launcher may still be closing
    }
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    RemoveWork(std::wstring(temp) + L"re5solo_update");  // best effort: the unpacked files from a zip update
}
