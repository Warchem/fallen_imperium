// Twilight Imperium (Miyoo Mini Plus homebrew) - program entry point.
//
//   ti4                          normal start (picks a screen method automatically)
//   ti4 --backend fbdev|sdl      force a screen method
//   ti4 --headless SCRIPT OUTDIR automated test run on a PC
//   ti4 --selftest               check the pixel conversion code
//   ti4 --simulate N [players] [seed] [-v]   AI-only games to test the rules
//
// On Windows the program opens a window (the "win32" screen method) and takes
// its files (settings, save, log, icons.dat) from the folder the .exe is in.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif
#if defined(__linux__)
#include <execinfo.h>
#endif

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "app/app.h"
#include "app/bug_report.h"
#include "audio/audio.h"
#include "core/log.h"
#include "core/paths.h"
#include "core/settings.h"
#include "ai/ai.h"
#include "engine/engine.h"
#include "game/session.h"
#include "game/profile.h"
#include "platform/platform.h"

namespace {
volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }
// 0.30.0: a crash leaves a line in ti4.log saying what the game was doing (the bug report keeps it)
void onCrash(int sig) {
    LOG("CRASH: signal %d while: %s", sig, logx::lastCrumb());
#if defined(__linux__)
    {  // 0.43.3: the stack - where in the program it happened (the build's .map file turns the addresses into names)
        void* frames[48];
        const int n = backtrace(frames, 48);
        LOG("CRASH: stack, %d frames:", n);
        if (logx::fd() >= 0) backtrace_symbols_fd(frames, n, logx::fd());
    }
#endif
    logx::close();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
}  // namespace

#ifdef _WIN32
namespace {
// 0.43.3: on a crash, the address it happened at and the calls that led there (as offsets into the .exe - the
// build's .map file names them), then the C runtime's own handler (the signal handler above writes the CRASH line)
LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;
LONG WINAPI winCrashFilter(EXCEPTION_POINTERS* ep) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0 && ep && ep->ContextRecord && ep->ExceptionRecord) {
        const uintptr_t base = uintptr_t(GetModuleHandleW(nullptr));
        CONTEXT ctx = *ep->ContextRecord;
        LOG("CRASH: exception 0x%08lx at exe+0x%llx (exe loaded at 0x%llx)", (unsigned long)ep->ExceptionRecord->ExceptionCode,
            (unsigned long long)(uintptr_t(ep->ExceptionRecord->ExceptionAddress) - base), (unsigned long long)base);
#if defined(_M_X64) || defined(__x86_64__)
        for (int i = 0; i < 32 && ctx.Rip; ++i) {
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
            LOG("CRASH:   #%d %s+0x%llx", i, imageBase == base ? "exe" : "dll", (unsigned long long)(ctx.Rip - (imageBase ? imageBase : 0)));
            if (!fn) {  // a leaf function: the return address is on top of the stack
                if (IsBadReadPtr(reinterpret_cast<const void*>(ctx.Rsp), 8)) break;
                ctx.Rip = *reinterpret_cast<const DWORD64*>(ctx.Rsp);
                ctx.Rsp += 8;
            } else {
                PVOID handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
            }
        }
#endif
    }
    return g_prevFilter ? g_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace
#endif

int runSelfTest();
int runUiTest(const std::string& outDir);
int runPreview(const std::string& outDir);
int runWarSunTest(const std::string& outDir);  // (0.73.0) the War Sun scene's frames
namespace ti {
int runSimulation(int games, int players, uint32_t seed, bool verbose);
int runDumpData();
int runAudioTest(const char* dir, const char* music);
}

#ifdef _WIN32
namespace {
// Work from the .exe's folder, wherever it was started from, and let
// command-line runs (--simulate...) print to the console that started them.
void windowsStartup(int argc) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        for (DWORD i = n; i > 0; --i)
            if (path[i - 1] == L'\\' || path[i - 1] == L'/') {
                path[i - 1] = 0;
                SetCurrentDirectoryW(path);
                break;
            }
    }
    if (argc > 1 && AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
}
}  // namespace
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    windowsStartup(argc);
#endif
#ifdef TI4_SDL2
    {  // Linux / Steam Deck: settings, save, log and icons.dat live next to the program
        char exe[1024];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n > 0) {
            exe[n] = 0;
            if (char* slash = std::strrchr(exe, '/')) {
                *slash = 0;
                if (chdir(exe) != 0) {}
            }
        }
    }
#endif
    std::string backend, script, outDir;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--selftest")) return runSelfTest() == 0 ? 0 : 1;
        if (!std::strcmp(argv[i], "--uitest") && i + 1 < argc) return runUiTest(argv[i + 1]);
        if (!std::strcmp(argv[i], "--preview") && i + 1 < argc) return runPreview(argv[i + 1]);
        if (!std::strcmp(argv[i], "--warsun") && i + 1 < argc) return runWarSunTest(argv[i + 1]);
        if (!std::strcmp(argv[i], "--simulate")) {
            int games = i + 1 < argc ? std::atoi(argv[i + 1]) : 10;
            int players = i + 2 < argc ? std::atoi(argv[i + 2]) : 0;
            uint32_t seed = i + 3 < argc ? uint32_t(std::atoi(argv[i + 3])) : 1;
            bool verbose = i + 4 < argc && !std::strcmp(argv[i + 4], "-v");
            return ti::runSimulation(games, players, seed, verbose);
        }
        if (!std::strcmp(argv[i], "--dumpdata")) return ti::runDumpData();
        if (!std::strcmp(argv[i], "--audiotest") && i + 1 < argc)  // every sound as a WAV + a mix (0.27.0)
            return ti::runAudioTest(argv[i + 1], i + 2 < argc ? argv[i + 2] : nullptr);  // systems + planets as JSON (make retro)
        if (!std::strcmp(argv[i], "--dumplog") && i + 1 < argc) {  // print a saved game's log (debugging)
            std::FILE* f = std::fopen(argv[i + 1], "rb");
            if (!f) return 1;
            std::string text;
            char buf[4096];
            for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
            std::fclose(f);
            // optional N: keep only the first N answers, then show the question waiting there and the AI's answer
            const int keep = i + 2 < argc ? std::atoi(argv[i + 2]) : -1;
            if (keep >= 0) {
                size_t a = text.find("\nanswers ");
                if (a != std::string::npos) {
                    size_t eol = text.find('\n', a + 1);
                    std::string out = text.substr(0, a + 1) + "answers " + std::to_string(keep) + "\n";
                    size_t pos = eol + 1;
                    for (int n = 0; n < keep && pos < text.size(); ++n) {
                        size_t nl = text.find('\n', pos);
                        if (nl == std::string::npos) nl = text.size() - 1;
                        out += text.substr(pos, nl - pos + 1);
                        pos = nl + 1;
                    }
                    text = out;
                }
            }
            ti::Engine e;
            std::string err;
            if (!e.loadText(text, &err)) {
                std::printf("load failed: %s\n", err.c_str());
                return 1;
            }
            for (const std::string& l : e.game().log) std::printf("%s\n", l.c_str());
            if (keep >= 0 && !e.finished()) {
                const ti::Decision& d = e.decision();
                std::printf("== pending: player %d tag %d: %s\n", d.player, int(d.tag), d.prompt.c_str());
                for (const ti::Option& o : d.options) std::printf("   option %d %s%s\n", o.id, o.label.c_str(), o.enabled ? "" : " (off)");
                ti::Rng rng(1);
                ti::Answer an = ti::aiAnswer(e.game(), d, rng);
                std::printf("== AI answer: %s\n", an.encode().c_str());
                // (2026-11-16, the tester's "bots stopped responding") TI4_DUMPPLAY=K (test only): is that answer accepted? then K more
                // answers by the AI for every seat - the new log lines, and the question it stops on if it gets stuck
                if (const char* dp = std::getenv("TI4_DUMPPLAY")) {
                    ti::Engine e2;
                    std::string err2;
                    e2.loadText(text, &err2);
                    const size_t logAt = e2.game().log.size();
                    int steps = 0, refused = 0;
                    const int most = std::atoi(dp);
                    std::string lastWhy, lastPrompt;
                    for (; steps < most && e2.waiting(); ++steps) {
                        const ti::Decision& dd = e2.decision();
                        ti::Answer a2 = ti::aiAnswer(e2.game(), dd, rng);
                        std::string why;
                        if (!e2.answer(a2, &why)) {
                            ++refused;
                            lastWhy = why;
                            lastPrompt = dd.prompt;
                            if (refused <= 5) std::printf("== REFUSED at step %d: player %d tag %d \"%s\" answer %s: %s\n", steps, dd.player, int(dd.tag),
                                                          dd.prompt.c_str(), a2.encode().c_str(), why.c_str());
                            if (refused >= 50) break;
                        }
                    }
                    const auto& lg = e2.game().log;
                    for (size_t li = logAt; li < lg.size(); ++li) std::printf("++ %s\n", lg[li].c_str());
                    std::printf("== played %d answers, %d refused, round %d, %s\n", steps, refused, e2.game().round,
                                e2.finished() ? "finished" : ("waiting: \"" + e2.decision().prompt + "\" (player " + std::to_string(e2.decision().player) + ")").c_str());
                }
                // board state (0.30.0 debugging): units per system, then each player's goods
                static const char* kU[] = {"Fi", "De", "Cr", "Ca", "Dr", "Fl", "WS", "In", "Me", "PDS", "SD", "Fl2", "Mon"};
                const ti::Game& g = e.game();
                auto force = [&](const ti::Force& f) {
                    std::string s;
                    for (int u = 0; u < ti::kUnitTypes && u < 13; ++u)
                        if (f.n[u]) s += " " + std::to_string(f.n[u]) + kU[u] + (f.dmg[u] ? "(" + std::to_string(f.dmg[u]) + "d)" : "");
                    return s;
                };
                for (size_t si = 0; si < g.systems.size(); ++si) {
                    const ti::SystemState& s = g.systems[si];
                    std::string line;
                    for (int p = 0; p < int(g.players.size()); ++p) {
                        std::string sp = force(s.space[p]);
                        if (!sp.empty()) line += "  P" + std::to_string(p) + " space:" + sp;
                        for (int16_t pi : s.planets) {
                            std::string pl = force(g.planets[pi].units[p]);
                            if (!pl.empty()) line += "  P" + std::to_string(p) + " " + ti::kPlanets[g.planets[pi].def].name + ":" + pl;
                        }
                    }
                    if (!line.empty()) std::printf("== sys %zu %s:%s\n", si, s.d().name, line.c_str());
                }
                for (int p = 0; p < int(g.players.size()); ++p) {
                    const ti::Player& pl = g.players[p];
                    std::printf("== P%d %s%s: %d VP, %d TG, %d comm, tokens %d/%d/%d, %zu ACs\n", p, ti::kFactions[pl.faction].name,
                                pl.human ? " (human)" : "", pl.vp, pl.tradeGoods, pl.commodities, pl.tactic, pl.fleet, pl.strategy,
                                pl.actionCards.size());
                }
            }
            return 0;
        }
        if (!std::strcmp(argv[i], "--backend") && i + 1 < argc) {
            backend = argv[++i];
        } else if (!std::strcmp(argv[i], "--headless") && i + 2 < argc) {
            script = argv[++i];
            outDir = argv[++i];
            backend = "headless";
        }
    }

    logx::open(backend == "headless" ? (outDir + "/ti4.log").c_str() : "ti4.log");
    LOG("Twilight Imperium homebrew %s (%s), built %s %s", TI4_VERSION, TI4_STAGE, __DATE__, __TIME__);
    char cwd[256] = "?";
    if (getcwd(cwd, sizeof cwd)) LOG("working folder: %s", cwd);

    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) std::signal(sig, onCrash);
#if defined(__linux__)
    {  // (loads the stack unwinder now rather than in the middle of a crash)
        void* warm[2];
        backtrace(warm, 2);
    }
#endif
#ifdef _WIN32
    g_prevFilter = SetUnhandledExceptionFilter(winCrashFilter);  // 0.43.3: the stack of a crash into ti4.log
#endif
    // 0.85.1 (owner: progress lost with every update): the profile, settings and autosave in a folder every version finds
    if (backend != "headless") paths::init();
    GameSession::savePath() = backend == "headless" ? outDir + "/savegame.ti4" : paths::user("savegame.ti4");
    // 0.43.3 (owner): after a crash the next start makes a bug report by itself (the title screen says where)
    if (backend != "headless" || std::getenv("TI4_CRASHCHECK")) bugreport::afterCrash();  // (TI4_CRASHCHECK: tests)

    Settings settings;
    settings.load(backend == "headless" ? outDir + "/settings.cfg" : paths::user("settings.cfg"));
    profile::load(backend == "headless" ? outDir + "/profile.cfg" : paths::user("profile.cfg"));  // 0.80.0: leagues finished, unlocks
#ifdef _WIN32
    if (backend.empty()) backend = "win32";
#elif defined(TI4_SDL2)
    if (backend.empty()) backend = "sdl2";
#else
    if (backend.empty()) backend = settings.get("backend", "auto");
#endif

    std::unique_ptr<Platform> platform;
    auto tryBackend = [&](const std::string& name) {
        std::unique_ptr<Platform> p;
        if (name == "fbdev") p = makeFbdevPlatform();
        else if (name == "sdl") p = makeSdlPlatform();
        else if (name == "headless") p = makeHeadlessPlatform(script, outDir);
        else if (name == "win32") p = makeWin32Platform();
        else if (name == "sdl2") p = makeSdl2Platform();
        if (!p) return false;
        LOG("trying screen method: %s", name.c_str());
        if (p->init()) {
            platform = std::move(p);
            return true;
        }
        p->shutdown();
        LOG("screen method %s failed", name.c_str());
        return false;
    };

    bool ok = false;
#ifdef _WIN32
    ok = tryBackend(backend == "headless" ? "headless" : "win32");
#elif defined(TI4_SDL2)
    ok = tryBackend(backend == "headless" ? "headless" : "sdl2");
#else
    if (backend == "auto") {
        ok = tryBackend("fbdev") || tryBackend("sdl");
    } else {
        ok = tryBackend(backend) || (backend != "headless" && (tryBackend("fbdev") || tryBackend("sdl")));
    }
#endif
    if (!ok) {
        LOG("FATAL: no screen method worked. Please send this log file.");
        logx::close();
#ifdef _WIN32
        MessageBoxW(nullptr, L"Fallen Imperium could not open its window. Details are in ti4.log.",
                    L"Fallen Imperium", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
    LOG("using screen method: %s", platform->name());
    // The Windows version shows keyboard keys in its hints instead of the Miyoo's buttons.
    setKeyboardKeys(std::string(platform->name()) == "win32" || std::getenv("TI4_KEYBOARD_KEYS"));
    // The Steam Deck build names the Deck's buttons (View = START, stick click = SELECT).
    setDeckKeys(std::string(platform->name()) == "sdl2" || std::getenv("TI4_DECK_KEYS"));
    platform->configure(settings);  // PC: picture, controls, full screen (0.27.0; may change the key names)

    // Quick check that the rules engine (which runs as a coroutine) works on
    // this device; if the app dies here, the log shows how far it got.
    if (backend != "headless") {
        LOG("engine self-test: starting");
        uint32_t t0 = monotonicMs();
        ti::Engine e;
        ti::Setup s;
        s.players = 4;
        s.human = -1;
        s.seed = 99;
        e.start(s);
        ti::Rng rng(1);
        int n = 0;
        for (; n < 200 && e.waiting(); ++n) {
            ti::Answer a = ti::aiAnswer(e.game(), e.decision(), rng);
            if (!e.answer(a)) break;
            e.takeEvents();
        }
        LOG("engine self-test: %d answers, round %d, %u ms, %s", n, e.game().round, monotonicMs() - t0,
            e.crashMessage().empty() ? "ok" : e.crashMessage().c_str());
    }

    if (backend != "headless") {  // sound effects + music (0.27.0)
        // 0.59.70 (owner): the title's reveal plays first - the music waits for it (the title scene releases it; TI4_NOINTRO=1 skips it)
        if (!std::getenv("TI4_NOINTRO")) audio::holdMusic();
        audio::start(settings);
    } else {
        audio::loadLibrary(settings);  // (0.68.0) the music player's list, without a sound output
    }

    int rc = 0;
    for (;;) {
        App app(*platform, settings);
        app.setStopFlag(&g_stop);
        app.push(makeTitleScene());
        rc = app.run();
        if (!app.switchRequested() || platform->headless()) break;
        // Rescue: swap fbdev <-> sdl and remember the choice.
        std::string next = std::string(platform->name()) == "fbdev" ? "sdl" : "fbdev";
        platform->shutdown();
        platform.reset();
        if (!tryBackend(next) && !tryBackend(next == "sdl" ? "fbdev" : "sdl")) {
            LOG("FATAL: could not reopen the screen");
            logx::close();
            return 1;
        }
        settings.set("backend", platform->name());
        settings.save();
        LOG("switched to screen method: %s", platform->name());
    }

    audio::stop(settings);
    platform->shutdown();
    LOG("bye");
    logx::close();
    return rc;
}
