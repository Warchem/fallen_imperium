// Headless backend for automated testing on a PC. Replays a script:
//   wait N        let N frames pass (one frame = 16 ms of game time)
//   press BTN     tap a button (down now, up 2 frames later)
//   hold BTN N    hold a button for N frames
//   down BTN / up BTN
//   shot NAME     save the most recently drawn frame as NAME.ppm
//   quit
// Lines starting with # are comments.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/log.h"
#include "platform/pixelconv.h"
#include "platform/platform.h"

namespace {

Btn parseBtn(const std::string& s) {
    for (int i = 0; i < kBtnCount; ++i)
        if (s == btnName(Btn(i))) return Btn(i);
    return Btn::None;
}

class HeadlessPlatform : public Platform {
public:
    HeadlessPlatform(std::string script, std::string outDir)
        : scriptPath_(std::move(script)), outDir_(std::move(outDir)), last_(640, 480) {}

    const char* name() const override { return "headless"; }
    bool headless() const override { return true; }

    bool init() override {
        std::ifstream in(scriptPath_);
        if (!in) {
            LOG("headless: cannot read script %s", scriptPath_.c_str());
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            lines_.push_back(line);
        }
        LOG("headless: %zu script lines", lines_.size());
        return true;
    }

    void shutdown() override {}

    void poll(std::vector<KeyEvent>& out, bool& quit) override {
        ++frame_;
        for (auto it = pendingUp_.begin(); it != pendingUp_.end();) {
            if (it->first <= frame_) {
                out.push_back({it->second, false, -1});
                it = pendingUp_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = pendingRaw_.begin(); it != pendingRaw_.end();) {  // mouse buttons let go (wildmash)
            if (it->frame <= frame_) {
                out.push_back(it->e);
                it = pendingRaw_.erase(it);
            } else {
                ++it;
            }
        }
        if (waitUntil_ > frame_) return;
        if (wildLeft_ > 0) {  // 0.43.0 (owner: a crash when scrolling the wheel): mouse + wheel + buttons at random
            wildRng_ = wildRng_ * 1103515245u + 12345u;
            const uint32_t r = wildRng_ >> 8;
            const int kind = int(r % 16);
            if (kind < 5) {
                KeyEvent e{Btn::None, false, kRawMouseMove};
                e.x = float((r >> 4) % 640);
                e.y = float((r >> 14) % 480);
                mx_ = e.x;
                my_ = e.y;
                out.push_back(e);
            } else if (kind < 9) {
                KeyEvent e{Btn::None, false, kRawWheel};
                e.x = float(int((r >> 4) % 7) - 3);
                out.push_back(e);
            } else if (kind < 11) {
                const int raw = kind == 9 ? kRawMouseLeft : kRawMouseRight;
                KeyEvent e{Btn::None, true, raw};
                e.x = mx_;
                e.y = my_;
                out.push_back(e);
                KeyEvent u = e;
                u.down = false;
                pendingRaw_.push_back({frame_ + 1, u});
            } else {
                static const Btn bs[] = {Btn::A, Btn::B, Btn::X, Btn::Y, Btn::Up, Btn::Down, Btn::Left, Btn::Right,
                                         Btn::Start, Btn::L1, Btn::R1, Btn::L2, Btn::R2, Btn::Select};
                const Btn b = bs[(r >> 4) % (sizeof(bs) / sizeof(bs[0]))];
                out.push_back({b, true, -1});
                pendingUp_.push_back({frame_ + 1, b});
            }
            waitUntil_ = frame_ + 2;
            --wildLeft_;
            return;
        }
        if (mashLeft_ > 0) {
            static const Btn pattern[] = {Btn::A, Btn::Start, Btn::Right, Btn::A, Btn::Start, Btn::Down, Btn::X, Btn::A,
                                          Btn::Start, Btn::Up, Btn::Right, Btn::Start, Btn::Y, Btn::B, Btn::X, Btn::A, Btn::Start};
            Btn b = pattern[mashStep_++ % (sizeof(pattern) / sizeof(pattern[0]))];
            out.push_back({b, true, -1});
            pendingUp_.push_back({frame_ + 1, b});
            waitUntil_ = frame_ + 3;
            --mashLeft_;
            return;
        }
        while (pc_ < lines_.size()) {
            std::istringstream ss(lines_[pc_++]);
            std::string cmd, arg;
            int n = 0;
            ss >> cmd >> arg >> n;
            if (cmd == "wait") {
                waitUntil_ = frame_ + std::max(1, std::atoi(arg.c_str()));
                return;
            } else if (cmd == "press" || cmd == "hold" || cmd == "down") {
                Btn b = parseBtn(arg);
                if (b == Btn::None) {
                    LOG("headless: unknown button %s", arg.c_str());
                    continue;
                }
                out.push_back({b, true, -1});
                if (cmd == "press") pendingUp_.push_back({frame_ + 2, b});
                if (cmd == "hold") pendingUp_.push_back({frame_ + std::max(1, n), b});
            } else if (cmd == "up") {
                out.push_back({parseBtn(arg), false, -1});
            } else if (cmd == "mouse") {  // (0.33.1) "mouse X Y": the pointer moves there (game units)
                KeyEvent e{Btn::None, false, kRawMouseMove};
                e.x = float(std::atof(arg.c_str()));
                e.y = float(n);
                out.push_back(e);
            } else if (cmd == "ldown" || cmd == "lup") {  // (0.47.0) "ldown X Y" / "lup X Y": the left button alone (drags)
                KeyEvent e{Btn::None, cmd == "ldown", kRawMouseLeft};
                e.x = float(std::atof(arg.c_str()));
                e.y = float(n);
                out.push_back(e);
            } else if (cmd == "click") {  // (0.46.2) "click X Y": the left button pressed and let go there (game units)
                KeyEvent e{Btn::None, true, kRawMouseLeft};
                e.x = float(std::atof(arg.c_str()));
                e.y = float(n);
                out.push_back(e);
                e.down = false;
                out.push_back(e);
            } else if (cmd == "type") {  // (0.63.0) "type 1234": digits typed on the keyboard
                for (char ch : arg)
                    if (ch >= '0' && ch <= '9') out.push_back({Btn::None, true, kRawDigit0 + (ch - '0')});
            } else if (cmd == "wheel") {  // "wheel N": N notches (negative = out) where the pointer is
                KeyEvent e{Btn::None, false, kRawWheel};
                e.x = float(std::atof(arg.c_str()));
                out.push_back(e);
            } else if (cmd == "wildmash") {  // "wildmash N SEED": N random mouse / wheel / button steps
                wildLeft_ = std::max(1, std::atoi(arg.c_str()));
                wildRng_ = uint32_t(n ? n : 1);
                return;
            } else if (cmd == "mash") {
                // press a fixed pattern of buttons for N steps (UI fuzzing)
                mashLeft_ = std::max(1, std::atoi(arg.c_str()));
                return;
            } else if (cmd == "shot") {
                saveShot(arg);
            } else if (cmd == "quit") {
                quit = true;
                return;
            }
        }
        if (pendingUp_.empty() && pendingRaw_.empty()) quit = true;  // script finished
    }

    // Goes through the same pixel conversion as the real screen, so the
    // screenshots also show the effect of the "flip" setting.
    void present(const Canvas& c, bool rotate) override {
        if (last_.width() != c.pw() || last_.height() != c.ph()) last_ = Canvas(c.pw(), c.ph());  // a sharp-picture test (TI4_SCALE)
        PixelFormatDesc fmt;
        convertFrame(c, reinterpret_cast<uint8_t*>(last_.pixels()), last_.width() * 4, last_.width(),
                     last_.height(), fmt, rotate);
        ++presents_;
    }

    bool defaultRotate() const override { return false; }
    std::string rawKeyName(int raw) const override { return "script " + std::to_string(raw); }
    std::string describe() const override {
        return "Backend: headless test runner\nScreen: " + std::to_string(desiredWidth()) + "x480 (virtual)\nScript: " + scriptPath_;
    }
    uint32_t ticks() override { return vt_; }
    // tests of the sharp PC picture: TI4_SCALE=2.25 draws everything 1440x1080
    float desiredScale() const override {
        const char* e = std::getenv("TI4_SCALE");
        return e ? std::max(1.f, std::min(4.f, float(std::atof(e)))) : 1.f;
    }
    // widescreen tests (0.46.0): TI4_SCREENW=854 draws a 16:9 screen (768 = the Steam Deck's 16:10)
    int desiredWidth() const override {
        const char* e = std::getenv("TI4_SCREENW");
        return e ? std::max(640, std::min(1200, std::atoi(e))) : 640;
    }
    void waitFrame(uint32_t, bool) override { vt_ += 16; }

private:
    void saveShot(const std::string& name) {
        std::string path = outDir_ + "/" + name + ".ppm";
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            LOG("headless: cannot write %s", path.c_str());
            return;
        }
        std::fprintf(f, "P6\n%d %d\n255\n", last_.width(), last_.height());
        std::vector<uint8_t> row(size_t(last_.width()) * 3);
        for (int y = 0; y < last_.height(); ++y) {
            const uint32_t* p = last_.pixels() + size_t(y) * last_.width();
            for (int x = 0; x < last_.width(); ++x) {
                row[x * 3 + 0] = uint8_t(p[x] >> 16);
                row[x * 3 + 1] = uint8_t(p[x] >> 8);
                row[x * 3 + 2] = uint8_t(p[x]);
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
        LOG("headless: saved %s (frame %d, %d presents)", path.c_str(), frame_, presents_);
    }

    std::string scriptPath_, outDir_;
    std::vector<std::string> lines_;
    size_t pc_ = 0;
    int frame_ = 0, waitUntil_ = 0, presents_ = 0, mashLeft_ = 0, mashStep_ = 0, wildLeft_ = 0;
    uint32_t wildRng_ = 1;
    float mx_ = 320.f, my_ = 240.f;
    struct PendingRaw {
        int frame;
        KeyEvent e;
    };
    std::vector<PendingRaw> pendingRaw_;
    std::vector<std::pair<int, Btn>> pendingUp_;
    Canvas last_;
    uint32_t vt_ = 1000;
};

}  // namespace

std::unique_ptr<Platform> makeHeadlessPlatform(const std::string& script, const std::string& outDir) {
    return std::unique_ptr<Platform>(new HeadlessPlatform(script, outDir));
}
