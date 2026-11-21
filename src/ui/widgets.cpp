#include "ui/widgets.h"

#include <memory>

#include <algorithm>
#include <cmath>
#include <random>

#include "core/input.h"
#include "core/log.h"
#include "ui/hits.h"
#include "ui/theme.h"

namespace {
struct Star {
    int x, y;
    uint8_t b;
    bool big;
};

const std::vector<Star>& stars() {
    static std::vector<Star> s;
    if (s.empty()) {
        std::mt19937 rng(4242);
        for (int i = 0; i < 260; ++i) {
            Star st;
            st.x = int(rng() % theme::kScreenW);
            st.y = int(rng() % theme::kScreenH);
            int r = int(rng() % 100);
            st.b = uint8_t(r < 70 ? 60 + rng() % 70 : 140 + rng() % 115);
            st.big = r > 95;
            s.push_back(st);
        }
    }
    return s;
}
}  // namespace

static void paintSpace(Canvas& c, bool planet) {
    c.verticalGradient(0, 0, c.width(), c.height(), theme::bgTop, theme::bg);
    // (widescreen, 0.46.0: the 640-wide star field repeats across a wider screen)
    for (int tx = 0; tx < c.width(); tx += theme::kScreenW)
        for (const Star& s : stars()) {
            const int x = s.x + tx;
            if (x >= c.width()) continue;
            uint32_t col = rgb(s.b, s.b, std::min(255, s.b + 20));
            if (s.big) {
                c.fillCircle(x + 0.5f, s.y + 0.5f, 1.3f, col);
            } else if (c.unscaled()) {
                c.pixel(x, s.y, col);
            } else {
                c.fillCircle(x + 0.5f, s.y + 0.5f, 0.62f, col);  // the sharp picture: a fine dot, not a block
            }
        }
    if (planet) {
        // A large dim planet in the lower right corner, lit from the upper left.
        const float dx = float(c.width() - theme::kScreenW);
        c.fillCircle(560 + dx, 600, 300, rgb(14, 22, 44));
        c.fillCircle(585 + dx, 625, 290, rgb(10, 15, 30));
        c.ring(560 + dx, 600, 300, 2.5f, rgb(70, 120, 200), 150);
    }
}

void drawSpaceBackground(Canvas& c, bool planet) {
    // Painted once, then copied: the gradient and stars never change.
    // (0.27.0: one per scale of the screen)
    static std::unique_ptr<Canvas> cache[2];
    int i = planet ? 1 : 0;
    if (!cache[i] || cache[i]->scale() != c.scale() || cache[i]->width() != c.fullWidth() || cache[i]->height() != c.height()) {
        cache[i].reset(new Canvas(c.fullWidth(), c.height(), c.scale()));
        paintSpace(*cache[i], planet);
    }
    // (widescreen: an older screen drawn centred still gets the whole background)
    const Rect clip = c.clipIsView() ? Rect{0, 0, c.pw(), c.ph()} : c.pxClip();
    const int W = c.pw();
    for (int y = clip.y; y < clip.y + clip.h; ++y)
        std::copy(cache[i]->pixels() + size_t(y) * W + clip.x, cache[i]->pixels() + size_t(y) * W + clip.x + clip.w,
                  c.pixels() + size_t(y) * W + clip.x);
}

CentredView::CentredView(Canvas& c, int width) : c_(c) {
    if (c.width() <= width) return;
    on_ = true;
    ox_ = c.viewX();
    vw_ = c.width();
    hits_ = hits::origin();
    clip_ = c.clip();
    dx_ = (vw_ - width) / 2;
    c.setView(ox_ + dx_, width);
    hits::origin() = c.viewX();
}

CentredView::~CentredView() {
    if (!on_) return;
    c_.setView(ox_, vw_);
    c_.setClip(clip_);
    hits::origin() = hits_;
}

void drawHoverLight(Canvas& c, Rect r, bool round) {
    if (r.w <= 2 || r.h <= 2) return;
    if (round) {
        const float cx = float(r.x) + float(r.w) / 2.f, cy = float(r.y) + float(r.h) / 2.f;
        const float rad = float(std::min(r.w, r.h)) / 2.f - 0.5f;
        c.fillCircle(cx, cy, rad, rgb(255, 214, 120), 26);
        c.ring(cx, cy, rad, 1.3f, theme::gold, 215);
        return;
    }
    const int rad = std::max(1, std::min(5, std::min(r.w, r.h) / 3));
    c.fillRoundRect(r.x, r.y, r.w, r.h, rad, rgb(255, 214, 120), 30);
    c.frameRoundRect(r.x, r.y, r.w, r.h, rad, theme::gold, 1, 215);
}

void drawPanel(Canvas& c, Rect r, uint32_t fill, uint32_t border, int radius, int alpha) {
    hits::addPanel(r);  // (0.61.2: the map underneath a panel takes no pointing or clicks)
    c.fillRoundRect(r.x, r.y, r.w, r.h, radius, fill, alpha);
    c.frameRoundRect(r.x, r.y, r.w, r.h, radius, border, 1);
}

int buttonBadgeWidth(const std::string& label) {
    return std::max(20, textWidth(FONT_SMB, keyLabel(label)) + 10);
}

int drawButtonBadge(Canvas& c, int x, int y, const std::string& label, bool lit) {
    const FontData& f = FONT_SMB;
    const std::string key = keyLabel(label);
    int w = buttonBadgeWidth(label), h = 18;
    c.fillRoundRect(x, y, w, h, 9, lit ? theme::gold : rgb(52, 62, 88));
    drawTextAligned(c, f, x, y + 1, w, Align::Center, key, lit ? theme::black : theme::text);
    return w;
}

void drawHintBar(Canvas& c, const std::vector<std::pair<std::string, std::string>>& hints) {
    const int h = 28, y = c.height() - h;
    c.fillRect(0, y, c.width(), h, rgb(5, 8, 16), 230);
    c.hline(0, c.width() - 1, y, theme::border);
    // Keyboard key names are longer than the Miyoo's buttons: tighten the
    // spacing, then use the small font, if the hints wouldn't fit.
    auto width = [&](const FontData& f, int gap) {
        int w = 10;
        for (const auto& hint : hints) w += buttonBadgeWidth(hint.first) + 5 + textWidth(f, hint.second) + gap;
        return w - gap;
    };
    const FontData* f = &FONT_SM;
    int gap = 14;
    if (width(*f, gap) > c.width() - 6) gap = 9;
    if (width(*f, gap) > c.width() - 6) f = &FONT_XS;
    int x = 10;
    for (const auto& hint : hints) {
        const int x0 = x;
        x += drawButtonBadge(c, x, y + 5, hint.first) + 5;
        x += drawText(c, *f, x, y + (f == &FONT_SM ? 6 : 7), hint.second, theme::dim);
        // (0.27.0) the hints are buttons for the mouse: a click presses that button
        static const std::pair<const char*, Btn> kBtns[] = {{"A", Btn::A},         {"B", Btn::B},         {"X", Btn::X},
                                                            {"Y", Btn::Y},         {"L1", Btn::L1},       {"R1", Btn::R1},
                                                            {"L2", Btn::L2},       {"R2", Btn::R2},       {"START", Btn::Start},
                                                            {"SELECT", Btn::Select}, {"MENU", Btn::Menu}};
        for (const auto& kb : kBtns)
            if (hint.first == kb.first) hits::addButton(Rect{x0 - 3, y + 2, x - x0 + 6, h - 3}, kb.second);
        x += gap;
    }
    if (x - gap > c.width()) {
        std::string all;
        for (const auto& hint : hints) all += keyLabel(hint.first) + " " + hint.second + "  ";
        LOG("hint bar too wide (%d px): %s", x - gap, all.c_str());
    }
}

void drawValueBadge(Canvas& c, int cx, int cy, int r, int value, uint32_t color, const FontData& f) {
    c.fillCircle(cx + 0.5f, cy + 0.5f, float(r), shade(color, 0.55f));
    c.ring(cx + 0.5f, cy + 0.5f, float(r), 1.6f, color);
    std::string v = std::to_string(value);
    drawTextAligned(c, f, cx - r, cy - f.lineHeight / 2, 2 * r + 1, Align::Center, v, theme::text);
}

void drawMenu(Canvas& c, Rect r, const std::vector<MenuItem>& items, int selected, uint32_t now, int* pick, int rowH) {
    for (size_t i = 0; i < items.size(); ++i) {
        int y = r.y + int(i) * rowH;
        const int boxH = rowH <= 30 ? rowH - 1 : rowH - 6;  // (0.69.0: twelve Options rows - the detail line stays inside)
        bool sel = int(i) == selected;
        if (pick) hits::add(Rect{r.x, y, r.w, rowH - 4}, [pick, i]() { *pick = int(i); });  // the mouse (0.27.0)
        if (sel) {
            float pulse = 0.5f + 0.5f * std::sin(float(now % 2000) / 2000.f * 6.2831853f);
            c.fillRoundRect(r.x, y, r.w, boxH, 8, theme::panelHi, 240);
            c.frameRoundRect(r.x, y, r.w, boxH, 8, mix(theme::goldDim, theme::gold, int(pulse * 255)), 2);
            // small marker triangle
            float tx = float(r.x + 12), ty = float(y + boxH / 2);
            float xs[3] = {tx, tx + 8, tx}, ys[3] = {ty - 6, ty, ty + 6};
            c.fillPolygon(xs, ys, 3, theme::gold);
        }
        const int dy = rowH <= 30 ? 1 : rowH < 40 ? 2 : 3, ddy = rowH <= 30 ? 15 : rowH < 40 ? 17 : 20;
        drawText(c, FONT_MDB, r.x + 28, y + dy, items[i].label, sel ? theme::text : theme::dim);
        if (!items[i].detail.empty())
            drawText(c, FONT_XS, r.x + 28, y + ddy, items[i].detail, sel ? theme::dim : theme::faint);
        // 0.43.0 (owner, P12): a row whose value turns with left / right shows the arrows - they are clickable
        static const std::string kArrows = "\xE2\x97\x80 \xE2\x96\xB6";
        if (sel && pick && items[i].detail.compare(0, kArrows.size(), kArrows) == 0) {
            const int aw = textWidth(FONT_XS, "\xE2\x97\x80 ");
            hits::addButton(Rect{r.x + 22, y + ddy - 3, aw + 2, 16}, Btn::Left);
            hits::addButton(Rect{r.x + 24 + aw, y + ddy - 3, aw + 4, 16}, Btn::Right);
        }
    }
}
