#include "ui/pharadn_ui.h"

#include <algorithm>
#include <cmath>

namespace pharadn {

uint32_t gold() { return rgb(255, 145, 4); }

// A tomb token: a dark bronze disc, a three-step pyramid in dull gold and a doorway at its foot that glows and fades slowly (each
// tomb a little out of step with the next, by where it stands).
void drawTomb(Canvas& c, float cx, float cy, float r, uint32_t now, int alpha) {
    if (r < 1.5f) return;
    const int a = alpha;
    c.fillCircle(cx, cy, r + std::max(1.f, r * 0.14f), rgb(8, 6, 2), a * 230 / 255);
    c.fillCircle(cx, cy, r, rgb(46, 38, 10), a);
    if (r >= 5.f) c.ring(cx, cy, r * 0.9f, std::max(1.f, r * 0.08f), rgb(120, 98, 30), a);
    // the pyramid: three steps, lit from the upper left
    const float base = cy + r * 0.5f, h = r * 0.95f, w = r * 1.25f;
    for (int s = 0; s < 3; ++s) {
        const float t0 = float(s) / 3.f, t1 = float(s + 1) / 3.f;
        const float y0 = base - h * t0, y1 = base - h * t1;
        const float hw = w * 0.5f * (1.f - t0 * 0.78f);
        float xs[4] = {cx - hw, cx + hw, cx + hw * 0.86f, cx - hw * 0.86f};
        float ys[4] = {y0, y0, y1, y1};
        c.fillPolygon(xs, ys, 4, s == 0 ? rgb(150, 124, 44) : s == 1 ? rgb(176, 148, 58) : rgb(204, 176, 80), a);
        if (r >= 7.f) {  // the shaded right face of each step
            float sx[4] = {cx, cx + hw, cx + hw * 0.86f, cx};
            float sy[4] = {y0, y0, y1, y1};
            c.fillPolygon(sx, sy, 4, rgb(96, 76, 22), a * 150 / 255);
        }
    }
    // the doorway and its glow
    const uint32_t phase = uint32_t(cx * 37.f + cy * 11.f) % 2800u;
    const float breathe = 0.55f + 0.45f * std::sin(float((now + phase) % 2800u) / 2800.f * 6.2831853f);
    const float dw = std::max(1.f, r * 0.24f), dh = std::max(1.f, r * 0.36f);
    c.fillCircle(cx, base - dh * 0.5f, r * 0.42f, gold(), int(float(a) * 0.22f * breathe));
    c.fillRect(int(cx - dw / 2.f + 0.5f), int(base - dh + 0.5f), std::max(1, int(dw + 0.5f)), std::max(1, int(dh + 0.5f)), gold(),
               int(float(a) * (0.6f + 0.4f * breathe)));
}

}  // namespace pharadn
