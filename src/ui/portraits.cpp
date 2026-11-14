#include "ui/portraits.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/log.h"
#include "engine/data.h"
#include "gfx/image.h"
#include "gfx/picture.h"

using namespace ti;

namespace portraits {
namespace {

#include "ui/gen/portrait_keys.inc"

constexpr int kRows = int(sizeof(kPortraitKeys) / sizeof(kPortraitKeys[0]));

struct Atlas {
    bool tried = false, ok = false;
    bool rows = false;  // (0.58.0, Miyoo) no whole atlas: one strip per faction, art/portraits/<key>, loaded when needed
    Image img;
    std::vector<int> rowOf;
};

Atlas& atlas() {
    static Atlas a;
    if (!a.tried) {
        a.tried = true;
        a.rowOf.assign(size_t(kFactionCount), -1);
        for (int f = 0; f < kFactionCount; ++f) {
            for (int r = 0; r < kRows; ++r)
                if (!std::strcmp(kPortraitKeys[r], kFactions[f].key)) a.rowOf[size_t(f)] = r;
        }
        a.ok = loadPicture("portraits", a.img) && a.img.w >= kPortraitFrames * kPortraitSize && a.img.h >= kRows * kPortraitSize;
        if (!a.ok) {
            // the Miyoo package: the whole atlas would take ~25 MB of its ~100 MB - it ships one strip per faction
            Image probe;
            a.rows = loadPicture(std::string("portraits/") + kPortraitKeys[0], probe) && probe.w >= kPortraitFrames * kPortraitSize;
            a.ok = a.rows;
        }
        LOG("portraits: %s (%dx%d)", a.ok ? (a.rows ? "strips" : "loaded") : "missing", a.img.w, a.img.h);
    }
    return a;
}

// where row `row` is: the atlas (y = row * size) or its own strip (y = 0); a few strips kept, least recent dropped
struct Source {
    const Image* img = nullptr;
    int y0 = 0;
};
Source rowSource(int row) {
    Atlas& a = atlas();
    if (!a.ok || row < 0) return {};
    if (!a.rows) return {&a.img, row * kPortraitSize};
    struct Strip {
        int row = -1;
        bool bad = false;
        uint32_t used = 0;
        Image img;
    };
    static Strip strips[4];
    static std::vector<char> missing(size_t(kRows), 0);
    static uint32_t tick = 0;
    ++tick;
    if (missing[size_t(row)]) return {};
    for (Strip& s : strips)
        if (s.row == row) {
            s.used = tick;
            return {&s.img, 0};
        }
    Strip* slot = &strips[0];
    for (Strip& s : strips)
        if (s.used < slot->used) slot = &s;
    Image img;
    if (!loadPicture(std::string("portraits/") + kPortraitKeys[row], img) || img.w < kPortraitFrames * kPortraitSize ||
        img.h < kPortraitSize) {
        missing[size_t(row)] = 1;
        return {};
    }
    slot->row = row;
    slot->used = tick;
    slot->img = std::move(img);
    return {&slot->img, 0};
}

// one frame enlarged to n x n real pixels (nearest: whole-pixel steps), with CRT scanlines
#if defined(__arm__)
constexpr bool kLean = true;  // the Miyoo: keep only the frame on screen (it changes every 125 ms; cheap to redo)
#else
constexpr bool kLean = false;
#endif
struct Scaled {
    std::vector<Image> frames;  // kPortraitFrames, made when first drawn (kLean: one, frames[0] = the last made)
    int shown = -1;
};

const Image& frameAt(int row, int frame, int n) {
    static std::map<uint64_t, Scaled> cache;
    static int gen = -1;
    if (gen != Canvas::scaleGeneration() || cache.size() > (kLean ? 4u : 24u)) {
        cache.clear();
        gen = Canvas::scaleGeneration();
    }
    Scaled& s = cache[(uint64_t(row) << 32) | uint64_t(n)];
    if (s.frames.empty()) s.frames.resize(kLean ? 1 : size_t(kPortraitFrames));
    Image& im = s.frames[kLean ? 0 : size_t(frame)];
    if (kLean && s.shown != frame) im = Image();
    if (im.empty()) {
        const Source src = rowSource(row);
        im.w = im.h = n;
        im.px.assign(size_t(n) * size_t(n), 0xFF000000u);
        s.shown = frame;
        if (!src.img) return im;
        const Image& a = *src.img;
        const int sx0 = frame * kPortraitSize, sy0 = src.y0;
        const int line = n >= 160 ? 3 : 2;  // the scanlines' pitch in real pixels
        for (int y = 0; y < n; ++y) {
            const uint32_t* from = &a.px[size_t(sy0 + y * kPortraitSize / n) * size_t(a.w) + size_t(sx0)];
            uint32_t* dst = &im.px[size_t(y) * size_t(n)];
            const bool dark = y % line == 0;
            for (int x = 0; x < n; ++x) {
                uint32_t p = from[x * kPortraitSize / n];
                if (dark) {  // 70 / 255 darker, like the mock-ups
                    const uint32_t r = ((p >> 16) & 255) * 185 / 255, g = ((p >> 8) & 255) * 185 / 255, b = (p & 255) * 185 / 255;
                    p = 0xFF000000u | (r << 16) | (g << 8) | b;
                }
                dst[x] = p;
            }
        }
    }
    return im;
}

}  // namespace

bool has(int faction) {
    const Atlas& a = atlas();
    return a.ok && faction >= 0 && faction < kFactionCount && a.rowOf[size_t(faction)] >= 0;
}

bool draw(Canvas& c, int faction, int x, int y, int size, uint32_t now, int alpha, int phase) {
    if (!has(faction) || size <= 0) return false;
    const int row = atlas().rowOf[size_t(faction)];
    if (!rowSource(row).img) return false;  // (a strip that can't be read: the caller draws the faction's icon)
    const int frame = int(((now + uint32_t(phase)) / uint32_t(kPortraitFrameMs)) % uint32_t(kPortraitFrames));
    // whole pixels: the largest multiple of 64 real pixels that fits, centred (the window's fill shows round it)
    // (unless that loses more than a fifth: then nearest at the full size)
    const int full = c.S(size), snap = full / kPortraitSize * kPortraitSize, n = snap * 5 >= full * 4 ? snap : full;
    const int off = (full - n) / 2;
    c.pxBlit(frameAt(row, frame, n), c.X(x) + off, c.Y(y) + off, alpha);
    return true;
}

uint32_t backdrop(int faction) {
    if (!has(faction)) return rgb(10, 12, 24);
    const Source src = rowSource(atlas().rowOf[size_t(faction)]);
    if (!src.img) return rgb(10, 12, 24);
    const uint32_t p = src.img->px[size_t(src.y0 + kPortraitSize - 1) * size_t(src.img->w)];  // bottom-left corner
    return p & 0xFFFFFF;
}

}  // namespace portraits
