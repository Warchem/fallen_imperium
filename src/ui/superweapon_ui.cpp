#include "ui/superweapon_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/data.h"
#include "engine/query.h"
#include "gfx/font.h"
#include "gfx/image.h"
#include "gfx/picture.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace superweapon {
using namespace ti;

namespace {
constexpr int kArtW = 288, kArtH = 176, kFrames = 64, kFrameMs = 100;  // (tools/gen_superweapon_art.py)
constexpr int kSphere = 64;

struct Art {
    bool tried = false, ok = false;
    Image img;
};
Art g_fortress, g_spheres;

bool loadFile(const std::string& path, Image& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> b;
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    return decodePicture(b.data(), b.size(), out);
}
const Image* art(Art& a, const char* name, int w, int h) {
    if (!a.tried) {
        a.tried = true;
        a.ok = loadFile(std::string("art/") + name, a.img) || loadFile(std::string("assets/art/") + name, a.img);
        a.ok = a.ok && a.img.w >= w && a.img.h >= h;
    }
    return a.ok ? &a.img : nullptr;
}

// one frame (sx, sy, sw x sh of the strip) drawn into the game-unit rectangle r, nearest-neighbour, at the real pixels
void blitScaled(Canvas& c, const Image& img, int sx, int sy, int sw, int sh, Rect r, int alpha = 255) {
    const int x0 = c.X(r.x), x1 = c.X(r.x + r.w), y0 = c.Y(r.y), y1 = c.Y(r.y + r.h);
    if (x1 <= x0 || y1 <= y0) return;
    std::vector<int> colAt(size_t(x1 - x0));
    for (int x = x0; x < x1; ++x) colAt[size_t(x - x0)] = sx + std::min(sw - 1, (x - x0) * sw / (x1 - x0));
    for (int y = y0; y < y1; ++y) {
        const int ay = sy + std::min(sh - 1, (y - y0) * sh / (y1 - y0));
        const uint32_t* row = &img.px[size_t(ay) * size_t(img.w)];
        int runStart = x0;
        uint32_t runCol = row[colAt[0]];
        for (int x = x0 + 1; x <= x1; ++x) {
            const uint32_t col = x < x1 ? row[colAt[size_t(x - x0)]] : ~runCol;
            if (col != runCol) {
                const int a = int((runCol >> 24) & 255) * alpha / 255;
                if (a > 0) c.pxSpan(runStart, x, y, runCol & 0xFFFFFFu, a);
                runStart = x;
                runCol = col;
            }
        }
    }
}

std::string where(const Game& g, int k) {
    const int x = superweaponPlanet(g, k);
    return x >= 0 ? std::string("on ") + kPlanets[g.planets[size_t(x)].def].name : std::string();
}
}  // namespace

uint32_t colour(int k) {
    static const uint32_t c[kSuperweaponCount] = {rgb(190, 90, 255), rgb(255, 50, 40), rgb(70, 220, 255), rgb(255, 170, 50), rgb(220, 20, 60)};
    return k >= 0 && k < kSuperweaponCount ? c[k] : rgb(200, 200, 200);
}

// The sphere, drawn small: a dark rim, shaded grey (lit from the upper left), an oval recess turned a little toward us, the slit iris
// breathing in the superweapon's colour (dimmed when its card is exhausted)
void drawPiece(Canvas& c, int k, float cx, float cy, float r, uint32_t now, bool exhausted, int alpha) {
    if (r < 1.5f) return;
    const int a = exhausted ? alpha * 3 / 5 : alpha;
    c.fillCircle(cx, cy, r + std::max(1.f, r * 0.14f), rgb(6, 6, 10), a * 230 / 255);
    c.fillCircle(cx, cy, r, rgb(78, 82, 88), a);
    c.fillCircle(cx - r * 0.14f, cy - r * 0.14f, r * 0.8f, rgb(128, 132, 140), a);
    c.fillCircle(cx - r * 0.3f, cy - r * 0.3f, r * 0.45f, rgb(178, 182, 190), a);
    // the recess
    float xs[16], ys[16];
    const float ex = cx + r * 0.18f, ey = cy + r * 0.04f;
    for (int i = 0; i < 16; ++i) {
        const float t = float(i) * 6.2831853f / 16.f;
        xs[i] = ex + std::cos(t) * r * 0.42f;
        ys[i] = ey + std::sin(t) * r * 0.5f;
    }
    c.fillPolygon(xs, ys, 16, rgb(30, 32, 38), a);
    if (r >= 6.f) c.ring(ex, ey, r * 0.3f, std::max(1.f, r * 0.06f), rgb(70, 72, 80), a);
    // the iris
    const float breathe = exhausted ? 0.25f : 0.6f + 0.4f * std::sin(float(now % 3200u) / 3200.f * 6.2831853f);
    const uint32_t col = colour(k);
    const float w = std::max(1.f, r * 0.12f), h = r * 0.38f;
    c.fillCircle(ex, ey, r * 0.3f, col, int(float(a) * 0.25f * breathe));
    c.fillRect(int(ex - w / 2.f + 0.5f), int(ey - h), std::max(1, int(w + 0.5f)), std::max(1, int(2.f * h)), col, int(float(a) * (0.55f + 0.45f * breathe)));
}

void drawWindow(Canvas& c, const Game& g, int p, uint32_t now) {
    c.fillRect(0, 0, 640, 480, rgb(0, 0, 0), 170);
    const Rect r{6, 6, 628, 444};
    drawPanel(c, r, rgb(10, 12, 13), rgb(170, 70, 60), 10);
    drawTextAligned(c, FONT_LG, r.x, r.y + 6, r.w, Align::Center, "SUPERWEAPONS", rgb(240, 226, 214));
    const std::string sub = p >= 0 && p < g.numPlayers ? playerName(g, p) + " - Classified Developments" : std::string("Classified Developments");
    drawTextAligned(c, FONT_XS, r.x, r.y + 27, r.w, Align::Center, sub, rgb(190, 160, 150));
    // the centre: the megastructure (3x the art on a 1440 x 1080 screen)
    const int aw = 312, ah = aw * kArtH / kArtW;
    const Rect A{320 - aw / 2, r.y + 42, aw, ah};
    const int frame = int((now / kFrameMs) % kFrames);
    if (const Image* img = art(g_fortress, "superweapon_fortress.png", kArtW * kFrames, kArtH))
        blitScaled(c, *img, frame * kArtW, 0, kArtW, kArtH, A);
    else {
        c.fillRect(A.x, A.y, A.w, A.h, rgb(14, 24, 26));
        drawPiece(c, kSwMors, float(A.x + A.w / 2), float(A.y + A.h / 2), float(ah) * 0.35f, now, false);
    }
    c.frameRect(A.x - 1, A.y - 1, A.w + 2, A.h + 2, rgb(190, 70, 60), 1);
    // the cards: two on each side, one under the picture
    const Player* pl = p >= 0 && p < g.numPlayers ? &g.players[size_t(p)] : nullptr;
    const int cw = 152, gap = 6, ch = (r.y + r.h - 6 - A.y - gap) / 2;
    const Rect places[kSuperweaponCount] = {Rect{r.x + 6, A.y, cw, ch}, Rect{r.x + 6, A.y + ch + gap, cw, ch},
                                            Rect{r.x + r.w - 6 - cw, A.y, cw, ch}, Rect{r.x + r.w - 6 - cw, A.y + ch + gap, cw, ch},
                                            Rect{320 - 150, A.y + ah + gap, 300, r.y + r.h - (A.y + ah + gap) - 6}};
    const Image* sph = art(g_spheres, "superweapon_spheres.png", kSphere * kFrames, kSphere * kSuperweaponCount);
    for (int k = 0; k < kSuperweaponCount; ++k) {
        const Rect R = places[k];
        const bool built = pl && ((pl->swCards >> k) & 1), purged = pl && ((pl->swPurged >> k) & 1);
        const bool exhausted = built && ((pl->swExhausted >> k) & 1);
        const bool dim = !built || exhausted;
        const uint32_t edge = built && !exhausted ? rgb(190, 70, 60) : rgb(90, 70, 70);
        c.fillRoundRect(R.x, R.y, R.w, R.h, 6, rgb(14, 16, 18), 245);
        c.frameRoundRect(R.x, R.y, R.w, R.h, 6, edge, 2);
        c.fillRoundRect(R.x + 4, R.y + 4, R.w - 8, 18, 4, dim ? rgb(30, 28, 30) : rgb(44, 30, 28));
        drawText(c, FONT_SMB, R.x + 9, R.y + 6, kSuperweapons[k].name, dim ? rgb(140, 132, 130) : rgb(245, 232, 222));
        {  // its state at the header's right
            const char* lab = purged ? "PURGED" : !built ? "NOT BUILT" : exhausted ? "EXHAUSTED" : "READY";
            const uint32_t fg = purged || exhausted ? rgb(255, 140, 130) : !built ? rgb(170, 170, 180) : rgb(150, 255, 170);
            drawTextAligned(c, FONT_XS, R.x, R.y + 8, R.w - 9, Align::Right, lab, fg);
        }
        const int is = 36, ix = R.x + R.w / 2 - is / 2, iy = R.y + 24;
        if (sph) blitScaled(c, *sph, (purged ? 0 : frame) * kSphere, k * kSphere, kSphere, kSphere, Rect{ix, iy, is, is}, dim ? 120 : 255);
        else drawPiece(c, k, float(ix + is / 2), float(iy + is / 2), float(is) * 0.4f, now, dim);
        int y = iy + is + 2;
        const std::string loc = purged ? std::string("purged") : built ? where(g, k) : std::string("not built yet");
        drawTextAligned(c, FONT_XS, R.x, y, R.w, Align::Center, loc, dim ? rgb(120, 110, 110) : rgb(200, 175, 160));
        y += 13;
        const int bottom = R.y + R.h - 3;
        for (const std::string& line : wrapText(FONT_XS, kSuperweapons[k].text, R.w - 10)) {
            if (y + FONT_XS.lineHeight > bottom) break;
            drawText(c, FONT_XS, R.x + 5, y, line, dim ? rgb(125, 120, 118) : rgb(230, 226, 222));
            y += FONT_XS.lineHeight;
        }
    }
    drawHintBar(c, {{"B", "Back"}, {"L1/R1", "Other factions"}});
}

}  // namespace superweapon
