// Reusable screen pieces: backgrounds, panels, menus and the button-hint bar.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "gfx/canvas.h"
#include "gfx/font.h"

void drawSpaceBackground(Canvas& c, bool planet = true);

// Widescreen (0.46.0): while it lives, a screen laid out for 640 units is drawn
// centred in the current view (nothing changes when the view is 640 wide); its
// mouse rows and buttons move with it. Restores the view, clip and hit origin.
class CentredView {
public:
    explicit CentredView(Canvas& c, int width = 640);
    ~CentredView();
    CentredView(const CentredView&) = delete;
    CentredView& operator=(const CentredView&) = delete;
    int dx() const { return on_ ? dx_ : 0; }  // how far the screen moved across

private:
    Canvas& c_;
    bool on_ = false;
    int ox_ = 0, vw_ = 0, hits_ = 0, dx_ = 0;
    Rect clip_;
};
void drawPanel(Canvas& c, Rect r, uint32_t fill, uint32_t border, int radius = 8, int alpha = 235);
// (0.72.0, owner: "every button / row lights up under the pointer the same way") the light the app draws over the button
// or action under the pointer: a gold edge and a faint warm wash, round or boxed
void drawHoverLight(Canvas& c, Rect r, bool round);
// Small rounded badge with a button label inside ("A", "L1", ...). Returns width.
int drawButtonBadge(Canvas& c, int x, int y, const std::string& label, bool lit = false);
// Bottom bar of {button, action} hints.
void drawHintBar(Canvas& c, const std::vector<std::pair<std::string, std::string>>& hints);
// Value badge (resource / influence circle with a number).
void drawValueBadge(Canvas& c, int cx, int cy, int r, int value, uint32_t color, const FontData& f);

struct MenuItem {
    std::string label;
    std::string detail;
};
// pick: the selection the mouse changes when it points at a row (0.27.0)
void drawMenu(Canvas& c, Rect r, const std::vector<MenuItem>& items, int selected, uint32_t now, int* pick = nullptr, int rowH = 40);
