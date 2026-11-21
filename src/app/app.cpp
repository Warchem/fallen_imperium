#include "app/app.h"

#include <algorithm>
#include <cmath>

#include "audio/audio.h"
#include "core/log.h"
#include "ui/hits.h"
#include "ui/pcolors.h"
#include "ui/theme.h"
#include "ui/widgets.h"

App::App(Platform& p, Settings& s)
    : platform_(p), settings_(s), canvas_(std::max(theme::kScreenW, p.desiredWidth()), theme::kScreenH, p.desiredScale()) {
    flip_ = settings_.getBool("flip", false);
    Canvas::screenScale() = canvas_.scale();
    theme::screenWRef() = canvas_.width();
    ++Canvas::scaleGeneration();
}

// The sharp PC picture (0.27.0): the window's size or the Screen option changed - draw at the new size.
// Widescreen (0.46.0): also the width in game units (the window's shape).
void App::checkScale() {
    const float want = platform_.desiredScale();
    const int wantW = std::max(theme::kScreenW, platform_.desiredWidth());
    if (std::fabs(want - canvas_.scale()) < 0.001f && wantW == canvas_.fullWidth()) return;
    canvas_ = Canvas(wantW, theme::kScreenH, want);
    Canvas::screenScale() = canvas_.scale();
    theme::screenWRef() = canvas_.width();
    ++Canvas::scaleGeneration();
    dirty_ = true;
    LOG("app: drawing at %dx%d (%dx%d units, scale %.3f)", canvas_.pw(), canvas_.ph(), canvas_.width(), canvas_.height(),
        double(canvas_.scale()));
}

void App::setFlip(bool f) {
    flip_ = f;
    settings_.setBool("flip", f);
    settings_.save();
    dirty_ = true;
    LOG("app: flip set to %d", int(f));
}

void App::push(std::unique_ptr<Scene> s) {
    stack_.push_back(std::move(s));
    hits::clear();  // they belonged to the screen underneath
    dirty_ = true;
}

void App::pop() {
    if (!stack_.empty()) {
        graveyard_.push_back(std::move(stack_.back()));
        stack_.pop_back();
    }
    hits::clear();
    dirty_ = true;
}

// The mouse and the keyboard camera (Windows / Steam Deck, 0.27.0) turned into
// what the screens understand: pointing selects (hits), a left click presses A
// (or the button under it), a right click presses B, a right drag and WASD
// move the map camera on screens that have one.
void App::mouseAndPan(const KeyEvent& e, uint32_t now) {
    Mouse& m = input_.mouseState();
    auto inject = [&](Btn b) {  // a press now, let go next frame
        input_.handle(b, true, now);
        injected_.push_back(b);
    };
    const bool pan = !stack_.empty() && stack_.back()->keysPan();
    switch (e.raw) {
        case kRawMouseLeave:  // (0.72.0) the pointer left the window / the focus went elsewhere
            m.inside = false;
            dirty_ = true;
            break;
        case kRawPanFast:  // (0.72.0) Shift: the camera goes faster
            if (panFast_ != e.down) LOG("app: camera faster %s", e.down ? "on" : "off");
            panFast_ = e.down;
            break;
        case kRawMouseMove: {
            const float dx = e.x - m.x, dy = e.y - m.y;
            m.inside = true;
            pointerAt_ = now;
            if (m.right && m.active) {
                dragDist_ += std::fabs(dx) + std::fabs(dy);
                if (dragDist_ > 5.f) m.dragging = true;
                if (m.dragging) {
                    m.dragX += dx;
                    m.dragY += dy;
                }
            }
            m.x = e.x;
            m.y = e.y;
            m.moved = true;
            m.active = true;
            input_.touch();
            break;
        }
        case kRawMouseLeft:
            m.active = true;
            m.x = e.x;
            m.y = e.y;
            if (e.down && !m.left) {
                m.left = true;
                m.leftPress = true;
                if (!stack_.empty() && !stack_.back()->wantsAllButtons()) {
                    const bool bare = !hits::any(e.x, e.y) && !stack_.back()->bareClickIsA(e.x, e.y);
                    const Btn b = bare ? Btn::None : hits::press(e.x, e.y);
                    if (b != Btn::None) {
                        input_.handle(b, true, now);  // held while the button is (hold A: faster)
                        leftBtn_ = b;
                    }
                } else {
                    input_.handle(Btn::A, true, now);
                    leftBtn_ = Btn::A;
                }
                dirty_ = true;
            } else if (!e.down && m.left) {
                m.left = false;
                m.click = true;
                if (leftBtn_ != Btn::None) input_.handle(leftBtn_, false, now);
                leftBtn_ = Btn::None;
            }
            break;
        case kRawMouseRight:
            m.active = true;
            if (e.down && !m.right) {
                m.right = true;
                m.dragging = false;
                dragDist_ = 0;
            } else if (!e.down && m.right) {
                m.right = false;
                if (!m.dragging) {
                    m.rightClick = true;
                    inject(Btn::B);
                }
                m.dragging = false;
            }
            input_.touch();
            break;
        case kRawWheel:
        case kRawWheelUp:
        case kRawWheelDown:
            m.wheel += e.raw == kRawWheel ? e.x : e.raw == kRawWheelUp ? 1.f : -1.f;
            m.active = true;
            input_.touch();
            break;
        case kRawDigit0 + 0: case kRawDigit0 + 1: case kRawDigit0 + 2: case kRawDigit0 + 3: case kRawDigit0 + 4:
        case kRawDigit0 + 5: case kRawDigit0 + 6: case kRawDigit0 + 7: case kRawDigit0 + 8: case kRawDigit0 + 9:
            if (e.down) input_.type(char('0' + (e.raw - kRawDigit0)));  // (0.63.0) number fields
            break;
        case kRawPanUp:
        case kRawPanDown:
        case kRawPanLeft:
        case kRawPanRight: {
            const int k = e.raw - kRawPanUp;
            static const Btn dpad[4] = {Btn::Up, Btn::Down, Btn::Left, Btn::Right};
            if (e.down && !panHeld_[k]) {
                panHeld_[k] = true;
                panAsDpad_[k] = !pan;  // screens without a map: WASD moves through the lists
                if (panAsDpad_[k]) input_.handle(dpad[k], true, now);
            } else if (!e.down && panHeld_[k]) {
                panHeld_[k] = false;
                if (panAsDpad_[k]) input_.handle(dpad[k], false, now);
                panAsDpad_[k] = false;
            }
            break;
        }
        default: break;
    }
}

// (0.72.0) the button or action under the pointer lights up the same way on every screen (hits::lit); rows show it by being
// selected. Not while a key or button was pressed after the mouse last moved, nor while dragging.
void App::drawPointerLight(const Scene& top) {
    const Mouse& m = input_.mouse();
    if (!m.active || !m.inside || m.dragging || top.wantsAllButtons() || int32_t(pointerAt_ - keyAt_) < 0) return;
    Rect r;
    hits::Light l = hits::Light::None;
    if (hits::lit(m.x, m.y, r, l)) drawHoverLight(canvas_, r, l == hits::Light::Round);
}

int App::run() {
    std::vector<KeyEvent> events;
    uint32_t fpsWindowStart = platform_.ticks();
    int fpsFrames = 0;
    bool q = false;
    while (!quit_ && !stack_.empty() && !(stopFlag_ && *stopFlag_)) {
        const uint32_t frameStart = platform_.ticks();
        input_.beginFrame();
        events.clear();
        q = false;
        platform_.poll(events, q);
        if (q) {
            LOG("app: quit requested by platform");
            break;
        }
        for (Btn b : injected_) input_.handle(b, false, frameStart);  // clicks from last frame let go
        injected_.clear();
        for (const KeyEvent& e : events) {
            if (e.down && e.raw != kRawMouseMove && e.raw != kRawMouseLeft && e.raw != kRawMouseRight && e.raw != kRawWheel)
                keyAt_ = frameStart;  // (0.72.0: a key or button puts the pointer's light out until the mouse moves)
            if (e.btn != Btn::None) input_.handle(e.btn, e.down, frameStart);
            else mouseAndPan(e, frameStart);
            if (e.raw == kRawMouseMove) continue;  // (not in the button test's list)
            keyLog_.push_back({e.btn, e.down, e.raw, frameStart});
            if (keyLog_.size() > 12) keyLog_.pop_front();
        }
        {  // WASD: the map camera, on a screen that has one
            const bool pan = !stack_.empty() && stack_.back()->keysPan();
            float px = 0, py = 0;
            if (pan) {
                px = float((panHeld_[3] && !panAsDpad_[3]) - (panHeld_[2] && !panAsDpad_[2]));
                py = float((panHeld_[1] && !panAsDpad_[1]) - (panHeld_[0] && !panAsDpad_[0]));
                // (0.72.0, owner: "Keep the edge scrolling off but available in the main menu options") the pointer at
                // the edge of the window moves the camera that way, while no mouse button is held
                const Mouse& m = input_.mouse();
                if (m.active && m.inside && !m.left && !m.right && settings_.getInt("edge_scroll", 0) != 0) {
                    const float mg = std::max(2.f, 8.f / canvas_.scale()), w = float(canvas_.width()), h = float(canvas_.height());
                    const float ex = m.x < mg ? -1.f : m.x > w - mg ? 1.f : 0.f, ey = m.y < mg ? -1.f : m.y > h - mg ? 1.f : 0.f;
                    const bool edge = ex != 0.f || ey != 0.f;
                    if (edge != edging_) LOG("app: edge scrolling %s", edge ? "starts" : "stops");
                    edging_ = edge;
                    px += ex;
                    py += ey;
                    px = std::max(-1.f, std::min(1.f, px));
                    py = std::max(-1.f, std::min(1.f, py));
                }
                if (panFast_) {  // (0.72.0, owner: WASD speed-up with Shift)
                    px *= 2.5f;
                    py *= 2.5f;
                }
            }
            input_.setPan(px, py);
        }
        // the wheel on screens without a map: up and down through the lists
        if (input_.mouse().wheel != 0.f && !stack_.empty() && stack_.back()->wheelScrolls(input_.mouse().x, input_.mouse().y) &&
            !stack_.back()->wantsAllButtons()) {
            wheelAcc_ += input_.mouse().wheel;
            while (wheelAcc_ >= 1.f) {
                input_.handle(Btn::Up, true, frameStart);
                injected_.push_back(Btn::Up);
                wheelAcc_ -= 1.f;
            }
            while (wheelAcc_ <= -1.f) {
                input_.handle(Btn::Down, true, frameStart);
                injected_.push_back(Btn::Down);
                wheelAcc_ += 1.f;
            }
        }
        // pointing at a row selects it
        if (input_.mouse().moved && !stack_.empty() && !stack_.back()->wantsAllButtons() &&
            hits::hover(input_.mouse().x, input_.mouse().y))
            dirty_ = true;
        input_.update(frameStart);
        if (input_.anyEvent()) dirty_ = true;
        // the tech blips (owner, 0.27.0): A / Space / left click confirms, B / Esc / right click goes back
        if (!stack_.back()->wantsAllButtons()) {
            if (input_.pressed(Btn::A)) audio::play(audio::Sfx::Confirm);
            else if (input_.pressed(Btn::B)) audio::play(audio::Sfx::Back);
        }
        if (platform_.screenRescue() && input_.heldFor(Btn::Select, frameStart) >= 3000 && !input_.held(Btn::Start) &&
            stack_.back()->allowsScreenRescue()) {
            LOG("app: SELECT held 3 s - switching screen method");
            switch_ = true;
            break;
        }

        checkScale();
        Scene* top = stack_.back().get();
        top->update(*this, input_, frameStart);
        if (stack_.empty() || quit_) break;
        top = stack_.back().get();

        const bool anim = top->animating();
        if (dirty_ || anim) {
            uint32_t t0 = monotonicMs();
            pcolours::setNow(frameStart);  // (0.62.0: the clock the new colours' finishes run on)
            hits::clear();  // the screen says again where its rows and buttons are
            if (top->wide() || canvas_.width() <= theme::kScreenW) {
                top->draw(*this, canvas_, frameStart);
            } else {  // widescreen (0.46.0): a screen laid out for 640 units, centred
                const int lx = theme::legacyX();
                canvas_.fillRect(0, 0, lx, canvas_.height(), rgb(0, 0, 0));
                canvas_.fillRect(lx + theme::kScreenW, 0, canvas_.width() - lx - theme::kScreenW, canvas_.height(), rgb(0, 0, 0));
                CentredView view(canvas_);
                top->draw(*this, canvas_, frameStart);
            }
            drawPointerLight(*top);
            uint32_t t1 = monotonicMs();
            platform_.present(canvas_, rotateOutput());
            uint32_t t2 = monotonicMs();
            drawMs_ = drawMs_ * 0.8f + float(t1 - t0) * 0.2f;
            presentMs_ = presentMs_ * 0.8f + float(t2 - t1) * 0.2f;
            dirty_ = false;
            ++fpsFrames;
        }
        graveyard_.clear();

        uint32_t now = platform_.ticks();
        if (now - fpsWindowStart >= 1000) {
            fps_ = float(fpsFrames) * 1000.f / float(now - fpsWindowStart);
            fpsFrames = 0;
            fpsWindowStart = now;
        }
        platform_.waitFrame(frameStart, !anim);
    }
    if (q || (stopFlag_ && *stopFlag_) || quit_)
        for (auto& sc : stack_) sc->onQuit();
    LOG("app: main loop finished");
    return 0;
}
