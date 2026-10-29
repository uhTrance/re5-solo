// Loose character files for in-engine scenes (the launcher's half of tools\loose_sheva.py).
//
// With a player slot closed, that character's archive is never loaded, but in-engine scenes still create
// their actor and ask for pawn\pl\pl0N\... files. RE5 falls back to loose files under nativePC_MT, so the
// launcher unpacks them from the player's own archives (uPl01ShebaNormal.arc for Sheva, uPl00ChrisNormal.arc
// for Chris when playing as Sheva, plus Chris's knife); no game files ship with the mod. ARC v7: "ARC\0", u16 version, u16 count,
// then 0x50-byte entries {char name[64], u32 type hash, u32 packed size, u32 size (low 29 bits), u32 offset};
// a packed entry is zlib. Every file written (or already there with the same bytes) is listed in
// re5_solo_loose.txt next to the exe; only listed files are removed on uninstall.
#include <windows.h>
#include <shlobj.h>

#include <string>
#include <vector>

namespace {

constexpr wchar_t kList[] = L"re5_solo_loose.txt";

struct Archive { const wchar_t* file; const char* prefix; };
constexpr Archive kArchives[] = {
    {L"uPl01ShebaNormal.arc", "pawn\\pl\\pl01\\"},
    {L"uPl00ChrisNormal.arc", "pawn\\pl\\pl00\\"},
    // Chris's knife. Some weapon mods give Sheva's knife (uWp0e.arc) Chris's knife model without packing it;
    // in co-op Chris always has it loaded, but with Chris removed the game asks for it loose and quits.
    {L"uWp01.arc", "pawn\\wp\\wp01\\"},
};

// Type hash -> extension for every type those entries use (re\arc_types.json).
struct Type { DWORD hash; const char* ext; };
constexpr Type kTypes[] = {
    {0x241F5DEB, "tex"}, {0x58A15856, "mod"}, {0x76820D81, "lmt"}, {0x0253F147, "hit"}, {0x4E2FEF36, "mtg"},
    {0x2282360D, "jex"}, {0x5802B3FF, "ahc"}, {0x3E363245, "chn"}, {0x0026E7FF, "ccl"}, {0x0DADAB62, "oba"},
    {0x1BA81D3C, "nck"},
};

const char* Extension(DWORD hash) {
    for (const auto& t : kTypes)
        if (t.hash == hash) return t.ext;
    return nullptr;
}

// --- zlib inflate (RFC 1950/1951), after Mark Adler's puff.c ----------------------------------------------

struct Huffman { short count[16]; short symbol[288]; };

class Inflater {
public:
    // Fails rather than write more than `limit` bytes to `out`.
    Inflater(const BYTE* in, size_t size, std::vector<BYTE>& out, size_t limit)
        : in_(in), size_(size), limit_(limit), out_(out) {}

    bool Run() {  // deflate, no preset dictionary
        if (size_ < 2 || (in_[0] & 0x0F) != 8 || (in_[1] & 0x20) || ((in_[0] << 8) | in_[1]) % 31) return false;
        pos_ = 2;
        for (int last = 0; !last;) {
            last = Bits(1);
            int type = Bits(2);
            bool ok = type == 0 ? Stored() : type == 1 ? Fixed() : type == 2 ? Dynamic() : false;
            if (!ok || bad_) return false;
        }
        return true;
    }

private:
    int Bits(int need) {
        long value = bitbuf_;
        while (bitcount_ < need) {
            if (pos_ >= size_) { bad_ = true; return 0; }
            value |= static_cast<long>(in_[pos_++]) << bitcount_;
            bitcount_ += 8;
        }
        bitbuf_ = value >> need;
        bitcount_ -= need;
        return static_cast<int>(value & ((1L << need) - 1));
    }

    bool Stored() {
        bitbuf_ = 0;
        bitcount_ = 0;
        if (pos_ + 4 > size_) return false;
        unsigned len = in_[pos_] | in_[pos_ + 1] << 8, nlen = in_[pos_ + 2] | in_[pos_ + 3] << 8;
        pos_ += 4;
        if (len != (~nlen & 0xFFFF) || pos_ + len > size_ || out_.size() + len > limit_) return false;
        out_.insert(out_.end(), in_ + pos_, in_ + pos_ + len);
        pos_ += len;
        return true;
    }

    int Decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= Bits(1);
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
            if (bad_) return -1;
        }
        return -1;
    }

    // Returns 0 for a complete code, > 0 for an incomplete one, < 0 for an over-subscribed one.
    static int Build(Huffman& h, const short* length, int n) {
        for (auto& c : h.count) c = 0;
        for (int s = 0; s < n; ++s) h.count[length[s]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left = (left << 1) - h.count[len];
            if (left < 0) return left;
        }
        short offset[16];
        offset[1] = 0;
        for (int len = 1; len < 15; ++len) offset[len + 1] = static_cast<short>(offset[len] + h.count[len]);
        for (int s = 0; s < n; ++s)
            if (length[s]) h.symbol[offset[length[s]]++] = static_cast<short>(s);
        return left;
    }

    bool Codes(const Huffman& lengths, const Huffman& distances) {
        static const short kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                           67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                            4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
                                            513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
                                             9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = Decode(lengths);
            if (symbol < 0 || bad_) return false;
            if (symbol < 256) {
                if (out_.size() >= limit_) return false;
                out_.push_back(static_cast<BYTE>(symbol));
            } else if (symbol == 256) {
                return true;
            } else {
                symbol -= 257;
                if (symbol >= 29) return false;
                int len = kLenBase[symbol] + Bits(kLenExtra[symbol]);
                int d = Decode(distances);
                if (d < 0 || d >= 30) return false;
                size_t dist = kDistBase[d] + Bits(kDistExtra[d]);
                if (bad_ || dist > out_.size() || out_.size() + len > limit_) return false;
                size_t from = out_.size() - dist;
                for (int i = 0; i < len; ++i) {
                    BYTE b = out_[from + i];  // by index: push_back may reallocate
                    out_.push_back(b);
                }
            }
        }
    }

    bool Fixed() {
        static Huffman lengths, distances;
        static bool built;
        if (!built) {
            short length[288];
            int s = 0;
            for (; s < 144; ++s) length[s] = 8;
            for (; s < 256; ++s) length[s] = 9;
            for (; s < 280; ++s) length[s] = 7;
            for (; s < 288; ++s) length[s] = 8;
            Build(lengths, length, 288);
            for (s = 0; s < 30; ++s) length[s] = 5;
            Build(distances, length, 30);
            built = true;
        }
        return Codes(lengths, distances);
    }

    bool Dynamic() {
        static const short kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = Bits(5) + 257, ndist = Bits(5) + 1, ncode = Bits(4) + 4;
        if (bad_ || nlen > 286 || ndist > 30) return false;
        short length[320] = {};
        for (int i = 0; i < ncode; ++i) length[kOrder[i]] = static_cast<short>(Bits(3));
        Huffman lengths, distances;
        if (Build(lengths, length, 19) != 0) return false;
        for (int index = 0; index < nlen + ndist;) {
            int symbol = Decode(lengths);
            if (symbol < 0 || bad_) return false;
            if (symbol < 16) {
                length[index++] = static_cast<short>(symbol);
                continue;
            }
            short repeat = 0;
            if (symbol == 16) {
                if (index == 0) return false;
                repeat = length[index - 1];
                symbol = 3 + Bits(2);
            } else {
                symbol = symbol == 17 ? 3 + Bits(3) : 11 + Bits(7);
            }
            if (index + symbol > nlen + ndist) return false;
            while (symbol--) length[index++] = repeat;
        }
        if (length[256] == 0) return false;
        int err = Build(lengths, length, nlen);
        if (err && (err < 0 || nlen != lengths.count[0] + lengths.count[1])) return false;
        err = Build(distances, length + nlen, ndist);
        if (err && (err < 0 || ndist != distances.count[0] + distances.count[1])) return false;
        return Codes(lengths, distances);
    }

    const BYTE* in_;
    size_t size_, limit_, pos_ = 0;
    long bitbuf_ = 0;
    int bitcount_ = 0;
    bool bad_ = false;
    std::vector<BYTE>& out_;
};

// --- files -----------------------------------------------------------------------------------------------

std::vector<BYTE> ReadFileBytes(const std::wstring& path) {
    std::vector<BYTE> data;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return data;
    LARGE_INTEGER size;
    if (GetFileSizeEx(f, &size) && size.QuadPart < (256 << 20)) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(f, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) || read != data.size())
            data.clear();
    }
    CloseHandle(f);
    return data;
}

bool WriteFileBytes(const std::wstring& path, const void* bytes, size_t size) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, bytes, static_cast<DWORD>(size), &written, nullptr) && written == size;
    CloseHandle(f);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }  // archive names are ASCII

std::vector<std::string> ReadList(const std::wstring& game) {
    auto bytes = ReadFileBytes(game + L"\\" + kList);
    std::vector<std::string> lines;
    std::string line;
    for (BYTE b : bytes) {
        if (b == '\n') { if (!line.empty()) lines.push_back(line); line.clear(); }
        else if (b != '\r') line += static_cast<char>(b);
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

}  // namespace

bool SceneFilesInstalled(const std::wstring& game) {
    return GetFileAttributesW((game + L"\\" + kList).c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Unpacks every character file; returns false (with `error`) if an archive can't be read or a file can't be
// written. Files already there with other contents belong to someone else and are left alone.
bool InstallSceneFiles(const std::wstring& game, std::wstring& error) {
    std::wstring root = game + L"\\nativePC_MT\\";
    std::vector<std::string> list = ReadList(game);
    auto same = [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) == 0; };
    auto listed = [&](const std::string& rel) {
        for (const auto& l : list) if (same(l, rel)) return true;
        return false;
    };
    auto unlist = [&](const std::string& rel) {
        std::erase_if(list, [&](const std::string& l) { return same(l, rel); });
    };
    for (const auto& archive : kArchives) {
        std::wstring arcPath = root + L"Image\\Archive\\" + archive.file;
        auto data = ReadFileBytes(arcPath);
        if (data.size() < 8 || memcmp(data.data(), "ARC\0", 4) != 0 || *reinterpret_cast<WORD*>(&data[4]) != 7) {
            error = L"Could not read " + arcPath + L".";
            return false;
        }
        int count = *reinterpret_cast<WORD*>(&data[6]);
        size_t prefixLen = strlen(archive.prefix);
        for (int i = 0; i < count; ++i) {
            size_t o = 8 + 0x50 * static_cast<size_t>(i);
            if (o + 0x50 > data.size()) break;
            std::string name(reinterpret_cast<const char*>(&data[o]), strnlen(reinterpret_cast<const char*>(&data[o]), 64));
            if (name.compare(0, prefixLen, archive.prefix) != 0) continue;
            DWORD hash, packed, size, offset;
            memcpy(&hash, &data[o + 64], 4);
            memcpy(&packed, &data[o + 68], 4);
            memcpy(&size, &data[o + 72], 4);
            memcpy(&offset, &data[o + 76], 4);
            size &= 0x1FFFFFFF;
            const char* ext = Extension(hash);
            if (!ext || static_cast<size_t>(offset) + packed > data.size()) continue;
            const BYTE* entry = data.data() + offset;
            std::vector<BYTE> raw;
            if (packed == size) {
                raw.assign(entry, entry + size);
            } else {
                raw.reserve(size);
                if (!Inflater(entry, packed, raw, size).Run() || raw.size() != size) {
                    error = L"Could not unpack " + Wide(name) + L" from " + archive.file + L".";
                    return false;
                }
            }
            std::string rel = name + "." + ext;
            std::wstring dest = root + Wide(rel);
            if (GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES) {
                // Same bytes: ours (claim it). Different bytes: someone else's, even if we once wrote it.
                bool ours = ReadFileBytes(dest) == raw;
                if (ours && !listed(rel)) list.push_back(rel);
                if (!ours) unlist(rel);
                continue;
            }
            std::wstring dir = dest.substr(0, dest.find_last_of(L'\\'));
            SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
            if (!WriteFileBytes(dest, raw.data(), raw.size())) {
                error = L"Could not write " + dest + L".";
                return false;
            }
            if (!listed(rel)) list.push_back(rel);
        }
    }
    std::string text;
    for (const auto& l : list) text += l + "\r\n";
    if (!WriteFileBytes(game + L"\\" + kList, text.data(), text.size())) {
        error = L"Could not write " + game + L"\\" + kList + L".";
        return false;
    }
    return true;
}

// Deletes every listed file, then any folders that left empty, and the list itself.
void RemoveSceneFiles(const std::wstring& game) {
    std::wstring root = game + L"\\nativePC_MT\\";
    for (const auto& rel : ReadList(game)) {
        std::wstring path = root + Wide(rel);
        DeleteFileW(path.c_str());
        for (size_t cut; (cut = path.find_last_of(L'\\')) != std::wstring::npos && path.size() > root.size();) {
            path.resize(cut);
            if (path.size() < root.size() || !RemoveDirectoryW(path.c_str())) break;
        }
    }
    DeleteFileW((game + L"\\" + kList).c_str());
}
