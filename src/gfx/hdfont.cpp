#include "gfx/hdfont.h"

#include <map>

#include "gfx/canvas.h"
#include "gfx/font.h"

#if defined(__arm__)  // the Miyoo: never scaled
namespace hdfont {
bool available() { return false; }
const Glyph* glyph(const FontData&, uint32_t, float) { return nullptr; }
}  // namespace hdfont
#else

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "gfx/hdfont_data.inc"

namespace hdfont {
namespace {

struct Face {
    stbtt_fontinfo info;
    bool ok = false;
};

Face* faces() {
    static Face f[5];
    static bool init = false;
    if (!init) {
        init = true;
        const unsigned char* data[5] = {kTtfRegular, kTtfBold, kTtfCondensed, kTtfCondensedBold, kTtfLazaxian};
        for (int i = 0; i < 5; ++i) f[i].ok = stbtt_InitFont(&f[i].info, data[i], stbtt_GetFontOffsetForIndex(data[i], 0)) != 0;
    }
    return f;
}

// the bitmap fonts' TrueType sources (tools/gen_font.py)
int faceOf(const FontData& f) {
    if (&f == &FONT_LAZ || &f == &FONT_LAZXL) return 4;  // the Lazaxian alphabet (0.57.0; XL 0.59.70)
    if (&f == &FONT_XS || &f == &FONT_SM) return 2;  // condensed
    if (&f == &FONT_SMB) return 3;                   // condensed bold
    if (&f == &FONT_MD) return 0;                    // regular
    return 1;                                        // bold: MDB, LG, XL
}

}  // namespace

bool available() { return faces()[0].ok; }

const Glyph* glyph(const FontData& f, uint32_t cp, float scale) {
    static std::map<uint64_t, Glyph> cache;
    static int gen = -1;
    if (gen != Canvas::scaleGeneration()) {
        cache.clear();
        gen = Canvas::scaleGeneration();
    }
    const int fi = faceOf(f);
    const uint64_t key = (uint64_t(fi) << 56) | (uint64_t(f.size & 255) << 48) | (uint64_t(uint32_t(scale * 1000.f)) << 24) | (cp & 0xFFFFFF);
    auto it = cache.find(key);
    if (it != cache.end()) return &it->second;
    Face& face = faces()[fi];
    if (!face.ok) return nullptr;
    if (cache.size() > 4000) cache.clear();
    Glyph& g = cache[key];
    const float px = float(f.size) * scale;
    const float k = stbtt_ScaleForMappingEmToPixels(&face.info, px);
    int adv = 0, lsb = 0;
    if (!stbtt_FindGlyphIndex(&face.info, int(cp))) cp = '?';
    stbtt_GetCodepointHMetrics(&face.info, int(cp), &adv, &lsb);
    g.adv = float(adv) * k;
    int w = 0, h = 0, xo = 0, yo = 0;
    unsigned char* bm = stbtt_GetCodepointBitmap(&face.info, k, k, int(cp), &w, &h, &xo, &yo);
    if (bm) {
        g.w = w;
        g.h = h;
        g.xo = xo;
        g.yo = yo;
        g.bits.assign(bm, bm + size_t(w) * size_t(h));
        stbtt_FreeBitmap(bm, nullptr);
    }
    return &g;
}

}  // namespace hdfont
#endif
