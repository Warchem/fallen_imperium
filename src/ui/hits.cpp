#include "ui/hits.h"

#include <cmath>

#include <vector>

namespace hits {
namespace {

struct Hit {
    Rect r;
    int kind;  // 0 row, 1 button, 2 action
    Btn b;
    std::function<void()> fn;
};
std::vector<Hit>& list() {
    static std::vector<Hit> v;
    return v;
}
bool inside(const Rect& r, float x, float y) { return x >= float(r.x) && y >= float(r.y) && x < float(r.x + r.w) && y < float(r.y + r.h); }
const Hit* at(float x, float y) {
    const auto& v = list();
    for (size_t i = v.size(); i-- > 0;)  // the last drawn is on top
        if (inside(v[i].r, x, y)) return &v[i];
    return nullptr;
}

}  // namespace

void clear() { list().clear(); }
int& origin() {
    static int x = 0;
    return x;
}
int& originY() {
    static int y = 0;
    return y;
}
float& scale() {
    static float k = 1.f;
    return k;
}
static Rect moved(Rect r) {
    const float k = scale();
    if (k != 1.f) {
        const int x0 = int(std::floor(float(r.x) * k + 0.5f)), y0 = int(std::floor(float(r.y) * k + 0.5f));
        const int x1 = int(std::floor(float(r.x + r.w) * k + 0.5f)), y1 = int(std::floor(float(r.y + r.h) * k + 0.5f));
        r = Rect{x0, y0, x1 - x0, y1 - y0};
    }
    r.x += origin();
    r.y += originY();
    return r;
}
void add(Rect r, std::function<void()> select) { list().push_back({moved(r), 0, Btn::A, std::move(select)}); }
void addButton(Rect r, Btn b) { list().push_back({moved(r), 1, b, {}}); }
void addAction(Rect r, std::function<void()> act) { list().push_back({moved(r), 2, Btn::None, std::move(act)}); }

bool hover(float x, float y) {
    const Hit* h = at(x, y);
    if (!h) return false;
    if (h->kind == 0 && h->fn) h->fn();
    return true;
}

bool any(float x, float y) { return at(x, y) != nullptr; }

Btn press(float x, float y) {
    const Hit* h = at(x, y);
    if (!h) return Btn::A;
    if (h->kind == 1) return h->b;
    if (h->kind == 2) {
        std::function<void()> act = h->fn;  // (the action may clear the list)
        if (act) act();
        return Btn::None;
    }
    if (h->fn) h->fn();
    return Btn::A;
}

}  // namespace hits
