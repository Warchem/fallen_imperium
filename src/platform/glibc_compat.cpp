// Linux desktop / Steam Deck build only: lets the program run on systems with
// an older glibc than the build machine's. The C library here (2.39) renames
// strtol / strtoul to C23 versions (glibc 2.38+) and libstdc++ uses
// arc4random (2.36+); these local stand-ins call the long-standing versions,
// so the program needs glibc 2.35 at most (SteamOS 3.5 and later have more).
// (0.27.0: fmodf too - glibc 2.38 gave it a new version. 0.58.3: sscanf too - the test code's sscanf had made the
// program need glibc 2.38 since 0.54.0.)
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <ctime>
#if defined(TI4_SDL2) && defined(__GLIBC__)

extern "C" {
long ti4_strtol_old(const char* s, char** end, int base) __asm__("strtol");
unsigned long ti4_strtoul_old(const char* s, char** end, int base) __asm__("strtoul");

long __isoc23_strtol(const char* s, char** end, int base) { return ti4_strtol_old(s, end, base); }
unsigned long __isoc23_strtoul(const char* s, char** end, int base) { return ti4_strtoul_old(s, end, base); }

int ti4_vsscanf_old(const char* s, const char* fmt, va_list ap) __asm__("vsscanf");
int __isoc23_sscanf(const char* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = ti4_vsscanf_old(s, fmt, ap);
    va_end(ap);
    return r;
}
int __isoc23_vsscanf(const char* s, const char* fmt, va_list ap) { return ti4_vsscanf_old(s, fmt, ap); }

// glibc 2.38 has a new fmodf; the game only needs it for animation phases
float fmodf(float x, float y) {
    if (y == 0.f) return 0.f;
    const double q = double(x) / double(y);
    const double t = q < 0 ? -double(int64_t(-q)) : double(int64_t(q));
    return float(double(x) - t * double(y));
}

uint32_t arc4random(void) {
    static uint64_t state = 0;
    if (!state) {
        if (FILE* f = std::fopen("/dev/urandom", "rb")) {
            if (std::fread(&state, sizeof state, 1, f) != 1) state = 0;
            std::fclose(f);
        }
        state ^= uint64_t(std::time(nullptr)) * 0x9E3779B97F4A7C15ull | 1;
    }
    state = state * 6364136223846793005ull + 1442695040888963407ull;  // PCG step
    const uint32_t x = uint32_t(((state >> 18) ^ state) >> 27);
    const uint32_t r = uint32_t(state >> 59);
    return (x >> r) | (x << ((32 - r) & 31));
}
}
#endif
