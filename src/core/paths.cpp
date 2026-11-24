// Where the player's own files live - see paths.h.
#include "core/paths.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include "core/log.h"

namespace paths {

namespace {
std::string g_dir;       // "" = the game's own folder
std::string g_imported;  // where the first start found the old files
int g_count = 0;
const char* const kFiles[] = {"profile.cfg", "settings.cfg", "savegame.ti4"};

#ifdef _WIN32
const char kSep = '\\';
#else
const char kSep = '/';
#endif

bool exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}
long long mtimeOf(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 ? (long long)st.st_mtime : -1;
}
bool makeDir(const std::string& p) {
#ifdef _WIN32
    return CreateDirectoryA(p.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(p.c_str(), 0755) == 0 || exists(p);
#endif
}
// every folder on the way, then the folder itself
bool makeDirs(const std::string& p) {
    for (size_t i = 1; i < p.size(); ++i)
        if ((p[i] == '/' || p[i] == '\\') && p[i - 1] != ':') makeDir(p.substr(0, i));
    return makeDir(p);
}
bool writable(const std::string& dir) {
    const std::string probe = dir + ".write_test";
    std::FILE* f = std::fopen(probe.c_str(), "wb");
    if (!f) return false;
    std::fclose(f);
    std::remove(probe.c_str());
    return true;
}
bool copyFile(const std::string& from, const std::string& to) {
    std::FILE* in = std::fopen(from.c_str(), "rb");
    if (!in) return false;
    std::FILE* out = std::fopen(to.c_str(), "wb");
    if (!out) {
        std::fclose(in);
        return false;
    }
    char buf[16384];
    size_t n;
    bool ok = true;
    while ((n = std::fread(buf, 1, sizeof buf, in)) > 0)
        if (std::fwrite(buf, 1, n, out) != n) ok = false;
    std::fclose(in);
    return std::fclose(out) == 0 && ok;
}
// the folders inside dir (names), at most `cap`
std::vector<std::string> subfolders(const std::string& dir, size_t cap) {
    std::vector<std::string> v;
#ifdef _WIN32
    _finddata_t fd;
    const intptr_t h = _findfirst((dir + "\\*").c_str(), &fd);
    if (h == -1) return v;
    do {
        if ((fd.attrib & _A_SUBDIR) && std::strcmp(fd.name, ".") && std::strcmp(fd.name, "..")) v.push_back(fd.name);
    } while (v.size() < cap && _findnext(h, &fd) == 0);
    _findclose(h);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return v;
    while (dirent* e = readdir(d)) {
        if (v.size() >= cap) break;
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
        struct stat st;
        if (stat((dir + "/" + e->d_name).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) v.push_back(e->d_name);
    }
    closedir(d);
#endif
    return v;
}

// the file is one of ours (its first line - other programs keep files called settings.cfg too)
bool ours(const std::string& p, const char* key) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    char line[160] = {};
    const bool got = std::fgets(line, sizeof line, f) != nullptr;
    std::fclose(f);
    if (!got) return false;
    return !std::strcmp(key, "profile.cfg") ? std::strstr(line, "Fallen Imperium - your profile") != nullptr
                                            : std::strstr(line, "Twilight Imperium (Miyoo) settings") != nullptr;
}

// The platform's folder for saved games, or "" (then the game's own folder is used).
std::string platformDir() {
    if (const char* e = std::getenv("TI4_USERDIR")) return e;  // (tests)
#if defined(__arm__)
    return "";  // the Miyoo: next to the game, as always
#elif defined(_WIN32)
    std::string base;
    {  // FOLDERID_SavedGames {4C5C32FF-BB9D-43b0-B5B4-2D72E54EAAA4} - loaded by hand (no link to shell32 / ole32)
        typedef HRESULT(WINAPI * KnownFolder)(const GUID&, DWORD, HANDLE, PWSTR*);
        typedef void(WINAPI * TaskFree)(void*);
        static const GUID kSavedGames = {0x4C5C32FF, 0xBB9D, 0x43b0, {0xB5, 0xB4, 0x2D, 0x72, 0xE5, 0x4E, 0xAA, 0xA4}};
        HMODULE sh = LoadLibraryA("shell32.dll"), ole = LoadLibraryA("ole32.dll");
        KnownFolder kf = sh ? (KnownFolder)(void*)GetProcAddress(sh, "SHGetKnownFolderPath") : nullptr;
        TaskFree tf = ole ? (TaskFree)(void*)GetProcAddress(ole, "CoTaskMemFree") : nullptr;
        PWSTR w = nullptr;
        if (kf && kf(kSavedGames, 0, nullptr, &w) == S_OK && w) {
            // the narrow (ANSI) path the file functions take - only when it says the same thing (a name with letters the
            // code page lacks would not): else the game's own folder
            BOOL lossy = FALSE;
            const int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, nullptr, 0, nullptr, &lossy);
            if (n > 1 && !lossy) {
                std::string s(size_t(n), '\0');
                WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, &s[0], n, nullptr, &lossy);
                s.resize(size_t(n - 1));
                if (!lossy) base = s;
            }
        }
        if (w && tf) tf(w);
    }
    if (base.empty())
        if (const char* up = std::getenv("USERPROFILE")) base = std::string(up) + "\\Saved Games";
    return base.empty() ? std::string() : base + "\\Fallen Imperium";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"))
        if (*x == '/') return std::string(x) + "/Fallen Imperium";
    if (const char* h = std::getenv("HOME"))
        if (*h) return std::string(h) + "/.local/share/Fallen Imperium";
    return "";
#endif
}

// The first start with the folder: the newest old copy of the files - in the game's own folder (unzipped over an older
// version, or copied there by hand) or in the folders beside it, one or two levels down (an older version unzipped next to
// this one). The folder whose profile.cfg is newest wins (else the newest settings.cfg); its files are copied in.
void importOld() {
    if (exists(g_dir + "profile.cfg") || exists(g_dir + "settings.cfg")) return;  // (not the first start)
    // (the Steam Deck's archive unpacks into TwilightImperium/ inside the version's folder: two levels up, then two down)
    std::vector<std::string> dirs = {std::string()};
    size_t seen = 0;
    for (const std::string& up : {std::string("..") + kSep, std::string("..") + kSep + ".." + kSep}) {
        for (const std::string& a : subfolders(up.substr(0, up.size() - 1), 400)) {
            dirs.push_back(up + a + kSep);
            for (const std::string& b : subfolders(up + a, 60)) {
                dirs.push_back(up + a + kSep + b + kSep);
                if (++seen > 4000) break;
            }
            if (seen > 4000) break;
        }
    }
    std::string best;
    long long bestT = -1;
    for (const char* key : {"profile.cfg", "settings.cfg"}) {
        for (const std::string& d : dirs) {
            const long long t = mtimeOf(d + key);
            if (t > bestT && ours(d + key, key)) bestT = t, best = d;
        }
        if (bestT >= 0) break;
    }
    if (bestT < 0) {
        LOG("paths: first start with %s - no older profile or settings found", g_dir.c_str());
        return;
    }
    for (const char* f : kFiles)
        if (exists(best + f) && !exists(g_dir + f) && copyFile(best + f, g_dir + f)) ++g_count;
    // (for the screen: "TI4-Windows-0.84.1", not "..\TI4-Windows-0.84.1\")
    std::string shown = best;
    while (shown.size() > 3 && shown.compare(0, 2, "..") == 0) shown = shown.substr(3);
    if (!shown.empty() && (shown.back() == '/' || shown.back() == '\\')) shown.pop_back();
    g_imported = best.empty() ? std::string("the game's folder") : "the folder " + shown;
    LOG("paths: brought %d files in from %s", g_count, g_imported.c_str());
}
}  // namespace

void init() {
    g_dir.clear();
    if (exists("portable.txt")) {
        LOG("paths: portable.txt - your files stay in the game's folder");
        return;
    }
    std::string d = platformDir();
    if (d.empty()) return;
    if (!makeDirs(d)) {
        LOG("paths: could not make %s - your files stay in the game's folder", d.c_str());
        return;
    }
    d += kSep;
    if (!writable(d)) {
        LOG("paths: can't write in %s - your files stay in the game's folder", d.c_str());
        return;
    }
    g_dir = d;
    LOG("paths: your profile, settings and saved game live in %s", g_dir.c_str());
#if !defined(__arm__)
    importOld();
#endif
}

const std::string& userDir() { return g_dir; }
std::string user(const std::string& file) { return g_dir + file; }
std::string shownDir() {
    if (g_dir.empty()) return "the game's folder";
    std::string s = g_dir;
    if (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}
const std::string& importedFrom() { return g_imported; }
int importedCount() { return g_count; }

bool openFolder() {
#ifdef _WIN32
    typedef HINSTANCE(WINAPI * Shell)(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT);
    HMODULE sh = LoadLibraryA("shell32.dll");
    Shell open = sh ? (Shell)(void*)GetProcAddress(sh, "ShellExecuteA") : nullptr;
    if (!open) return false;
    const std::string d = g_dir.empty() ? std::string(".") : shownDir();
    return (intptr_t)open(nullptr, "open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
#else
    return false;
#endif
}

}  // namespace paths
