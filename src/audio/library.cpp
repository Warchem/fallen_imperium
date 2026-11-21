// The player's own music (0.68.0, owner: "a music player for the game ... where the player can put their own music in a
// folder"): the MP3 files of the "My Music" folder next to the game (and of the folders inside it, two levels down), with
// their titles and artists from the files' tags (ID3v2, else ID3v1, else the file name: "Artist - Title.mp3").
#include "audio/library.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace audio {

namespace {

// the folder (TI4_MYMUSIC: another one - the tests, which run in the source tree)
std::string folderName() {
    const char* e = std::getenv("TI4_MYMUSIC");
    return e && *e ? std::string(e) : std::string("My Music");
}

#ifdef _WIN32
std::wstring wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(std::max(1, n)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}
std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(1, n)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}
#endif

void putUtf8(std::string& out, uint32_t c) {
    if (c < 0x80) out += char(c);
    else if (c < 0x800) {
        out += char(0xC0 | (c >> 6));
        out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += char(0xE0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    } else {
        out += char(0xF0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3F));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    }
}

// a tag's text (its first byte says how it is written) as UTF-8
std::string tagText(const uint8_t* p, size_t n) {
    if (n == 0) return "";
    const uint8_t enc = p[0];
    ++p;
    --n;
    std::string out;
    if (enc == 1 || enc == 2) {  // UTF-16 (with a byte-order mark) / UTF-16 big-endian
        bool be = enc == 2;
        size_t i = 0;
        if (enc == 1 && n >= 2) {
            if (p[0] == 0xFE && p[1] == 0xFF) be = true, i = 2;
            else if (p[0] == 0xFF && p[1] == 0xFE) be = false, i = 2;
        }
        for (; i + 1 < n; i += 2) {
            uint32_t c = be ? uint32_t(p[i]) << 8 | p[i + 1] : uint32_t(p[i + 1]) << 8 | p[i];
            if (c == 0) break;
            if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {  // a pair
                const uint32_t lo = be ? uint32_t(p[i + 2]) << 8 | p[i + 3] : uint32_t(p[i + 3]) << 8 | p[i + 2];
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
            putUtf8(out, c);
        }
    } else if (enc == 3) {  // UTF-8
        for (size_t i = 0; i < n && p[i]; ++i) out += char(p[i]);
    } else {  // Latin-1
        for (size_t i = 0; i < n && p[i]; ++i) putUtf8(out, p[i]);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\0')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

// (frame by frame, skipping the rest: a tag can hold a large cover picture, and a big folder is read at every start)
void readId3v2(std::FILE* f, std::string& title, std::string& artist) {
    uint8_t h[10];
    if (std::fread(h, 1, 10, f) != 10 || std::memcmp(h, "ID3", 3) != 0) return;
    const int ver = h[3];
    if (ver < 2 || ver > 4) return;
    auto syncsafe = [](const uint8_t* b) { return long(b[0] & 0x7F) << 21 | long(b[1] & 0x7F) << 14 | long(b[2] & 0x7F) << 7 | long(b[3] & 0x7F); };
    const long end = 10 + syncsafe(h + 6);
    long i = 10;
    if ((h[5] & 0x40) && ver >= 3) {  // an extended header: skip it
        uint8_t x[4];
        if (std::fread(x, 1, 4, f) != 4) return;
        i += ver == 4 ? syncsafe(x) : (long(x[0]) << 24 | long(x[1]) << 16 | long(x[2]) << 8 | long(x[3])) + 4;
    }
    const long idLen = ver == 2 ? 3 : 4, headLen = ver == 2 ? 6 : 10;
    std::vector<uint8_t> body;
    while (i + headLen <= end && (title.empty() || artist.empty())) {
        uint8_t fh[10];
        if (std::fseek(f, i, SEEK_SET) != 0 || std::fread(fh, 1, size_t(headLen), f) != size_t(headLen) || fh[0] == 0) break;
        const std::string id(reinterpret_cast<const char*>(fh), size_t(idLen));
        long n;
        if (ver == 2) n = long(fh[3]) << 16 | long(fh[4]) << 8 | long(fh[5]);
        else if (ver == 4) n = syncsafe(fh + 4);
        else n = long(fh[4]) << 24 | long(fh[5]) << 16 | long(fh[6]) << 8 | long(fh[7]);
        if (n <= 0 || i + headLen + n > end) break;
        std::string* into = id == "TIT2" || id == "TT2" ? &title : id == "TPE1" || id == "TP1" ? &artist : nullptr;
        if (into) {
            body.resize(size_t(std::min<long>(n, 4096)));
            body.resize(std::fread(body.data(), 1, body.size(), f));
            *into = tagText(body.data(), body.size());
        }
        i += headLen + n;
    }
}

void readId3v1(std::FILE* f, std::string& title, std::string& artist) {
    uint8_t t[128];
    if (std::fseek(f, -128, SEEK_END) != 0 || std::fread(t, 1, 128, f) != 128 || std::memcmp(t, "TAG", 3) != 0) return;
    auto field = [&](int at, int len) {
        std::string s;
        for (int k = 0; k < len && t[at + k]; ++k) putUtf8(s, t[at + k]);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };
    if (title.empty()) title = field(3, 30);
    if (artist.empty()) artist = field(33, 30);
}

bool isMp3(const std::string& name) {
    if (name.size() < 5) return false;
    std::string ext = name.substr(name.size() - 4);
    for (char& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return ext == ".mp3";
}
// other music files (not played: the player says how many it skipped)
bool isOtherAudio(const std::string& name) {
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    for (char& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return ext == "flac" || ext == "ogg" || ext == "oga" || ext == "opus" || ext == "wav" || ext == "m4a" || ext == "aac" ||
           ext == "wma" || ext == "aiff" || ext == "aif" || ext == "alac" || ext == "ape" || ext == "mp4";
}

std::string lower(std::string s) {
    for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// the files of one folder (the folders inside it first, then its songs), in name order
void scan(const std::string& dir, const std::string& rel, int depth, std::vector<Song>& out, int& others) {
    std::vector<std::string> dirs, files;
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wide(dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::string name = narrow(fd.cFileName);
        if (name == "." || name == ".." || (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) dirs.push_back(name);
        else if (isMp3(name)) files.push_back(name);
        else if (isOtherAudio(name)) ++others;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    const char sep = '\\';
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.empty() || name[0] == '.') continue;
        struct stat st;
        if (stat((dir + "/" + name).c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) dirs.push_back(name);
        else if (S_ISREG(st.st_mode) && isMp3(name)) files.push_back(name);
        else if (S_ISREG(st.st_mode) && isOtherAudio(name)) ++others;
    }
    closedir(d);
    const char sep = '/';
#endif
    auto byName = [](const std::string& a, const std::string& b) { return lower(a) < lower(b); };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    if (depth < 2)
        for (const std::string& sub : dirs) scan(dir + sep + sub, rel + sub + "/", depth + 1, out, others);
    for (const std::string& name : files) {
        Song s;
        s.path = dir + sep + name;
        s.file = rel + name;
        if (std::FILE* f = openFile(s.path, "rb")) {
            readId3v2(f, s.title, s.artist);
            if (s.title.empty() || s.artist.empty()) readId3v1(f, s.title, s.artist);
            std::fclose(f);
        }
        if (s.title.empty()) {  // the file name: "Artist - Title.mp3", "01 - Title.mp3", "Title.mp3"
            std::string stem = name.substr(0, name.size() - 4);
            const size_t dash = stem.find(" - ");
            if (dash != std::string::npos) {
                const std::string left = stem.substr(0, dash);
                const bool number = !left.empty() && std::all_of(left.begin(), left.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; });
                if (!number && s.artist.empty()) s.artist = left;
                stem = stem.substr(dash + 3);
            }
            s.title = stem;
        }
        out.push_back(s);
    }
}

}  // namespace

std::FILE* openFile(const std::string& path, const char* mode) {
#ifdef _WIN32
    return _wfopen(wide(path).c_str(), wide(mode).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

#ifdef _WIN32
bool canShowFolder() { return true; }
bool showFolder(const std::string& path) {  // (shell32 loaded when asked: the game doesn't link it)
    typedef HINSTANCE(WINAPI * ShellExec)(HWND, const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*, int);
    static HMODULE shell = LoadLibraryW(L"shell32.dll");
    ShellExec run = shell ? reinterpret_cast<ShellExec>(reinterpret_cast<void*>(GetProcAddress(shell, "ShellExecuteW"))) : nullptr;
    if (!run) return false;
    return reinterpret_cast<intptr_t>(run(nullptr, L"open", wide(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}
#else
bool canShowFolder() { return false; }
bool showFolder(const std::string&) { return false; }
#endif

std::string myMusicFolder(bool make) {
    const std::string name = folderName();
#ifdef _WIN32
    if (make) _wmkdir(wide(name).c_str());
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetFullPathNameW(wide(name).c_str(), DWORD(sizeof buf / sizeof buf[0]), buf, nullptr);
    return n > 0 && n < sizeof buf / sizeof buf[0] ? narrow(buf) : name;
#else
    if (make) mkdir(name.c_str(), 0755);
    if (!name.empty() && name[0] == '/') return name;
    char buf[4096];
    if (getcwd(buf, sizeof buf)) return std::string(buf) + "/" + name;
    return name;
#endif
}

std::vector<Song> findSongs(int* otherAudio) {
    std::vector<Song> out;
    int others = 0;
    scan(folderName(), "", 0, out, others);
    if (otherAudio) *otherAudio = others;
    return out;
}

}  // namespace audio
