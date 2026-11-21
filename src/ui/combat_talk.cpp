#include "ui/combat_talk.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "engine/data.h"
#include "engine/query.h"
#include "gfx/font.h"
#include "ui/codec.h"

namespace combattalk {
namespace {

constexpr float kSqrt3 = 1.7320508f;
constexpr uint32_t kSlide = 280;   // a portrait sliding in
constexpr uint32_t kSecond = 160;  // the defender's portrait comes in this much later
constexpr uint32_t kStartA = 600;  // the attacker starts talking
constexpr uint32_t kPause = 900;   // after the attacker's last word, before the defender answers
constexpr uint32_t kLinger = 1500; // after the defender's last word
constexpr uint32_t kFade = 450;
constexpr uint32_t kSwap = 160;    // the box changes speaker

uint32_t typeMs(const speech::Speech& s) { return uint32_t(s.chars) * 1000U / 55U; }

float easeOut(float t) {
    t = std::max(0.f, std::min(1.f, t));
    return 1.f - (1.f - t) * (1.f - t) * (1.f - t);
}

uint32_t bits(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

}  // namespace

void Talk::start(const Side& a, const Side& b, uint32_t now, uint32_t seed) {
    s_[0] = a;
    s_[1] = b;
    sp_[0] = speech::Speech();
    sp_[1] = speech::Speech();
    laidW_ = laidH_ = -1;
    t0_ = now;
    seed_ = seed;
    startB_ = kStartA + uint32_t(a.line.size()) * 1000U / 55U + kPause;  // (until the lines are laid out)
    on_ = true;
    said_[0] = said_[1] = false;
}

uint32_t Talk::endTime() const { return startB_ + typeMs(sp_[1]) + (s_[1].line.empty() ? 0U : kLinger); }

bool Talk::active(uint32_t now) const { return on_ && int32_t(now - (t0_ + endTime() + kFade)) < 0; }

int Talk::voiceDue(uint32_t now) {
    if (!on_) return -1;
    const uint32_t t = now - t0_;
    if (!said_[0] && t >= kStartA) {
        said_[0] = true;
        if (!s_[0].line.empty()) return 0;
    }
    if (!said_[1] && t >= startB_) {
        said_[1] = true;
        if (!s_[1].line.empty()) return 1;
    }
    return -1;
}

void Talk::layoutLines(int textW, int textH) {
    if (textW == laidW_ && textH == laidH_) return;
    laidW_ = textW;
    laidH_ = textH;
    for (int k = 0; k < 2; ++k)
        if (!s_[k].line.empty())
            sp_[k] = speech::layoutFit(s_[k].faction, s_[k].line, textW, textH, s_[k].col, rgb(238, 234, 240), seed_ >> (3 * k));
    startB_ = kStartA + typeMs(sp_[0]) + kPause;
}

void Talk::draw(Canvas& c, Rect area, int bottom, uint32_t now) {
    if (!active(now)) return;
    const uint32_t t = now - t0_;
    // the hex the battle is drawn in (as SpaceBattle / GroundBattle place it)
    const float R = std::min(float(area.w) / 2.f, float(area.h) / kSqrt3) - 4.f;
    const float cx = float(area.x) + float(area.w) / 2.f, cy = float(area.y) + float(area.h) / 2.f;
    const float left = cx - R;  // the hex's left corner
    // the largest portrait that stays outside the hex in the lower corner (its top right corner is the tight spot)
    int P = int((left - 12.f + (float(bottom) - cy) / kSqrt3) / (1.f + 1.f / kSqrt3));
    P = std::max(48, std::min(120, P));
    const int hexBottom = int(cy + R * 0.8660254f);
    const Rect box{8 + P + 10, hexBottom + 5, area.w - 2 * (8 + P + 10), bottom - (hexBottom + 5)};
    const Rect text{box.x + 10, box.y + 20, box.w - 20, box.h - 23};
    layoutLines(text.w, text.h);

    const uint32_t end = endTime();
    const int fadeA = t > end ? std::max(0, 255 - int((t - end) * 255U / kFade)) : 255;
    const int speaker = t + kSwap < startB_ ? 0 : 1;
    const bool talking = (speaker == 0 && t >= kStartA && t < kStartA + typeMs(sp_[0])) ||
                         (speaker == 1 && t >= startB_ && t < startB_ + typeMs(sp_[1]));

    // a dark stage under them: the lower corners outside the hex and the strip under it
    {
        const float yA = float(bottom - P - 24), h0 = float(area.y + area.h) - cy;
        const float eL = left + std::max(0.f, yA - cy) / kSqrt3, eR = cx + R - std::max(0.f, yA - cy) / kSqrt3;
        const float hb = float(hexBottom) + 0.5f;
        const float xs[8] = {float(area.x), eL, cx - R / 2.f, cx + R / 2.f, eR, float(area.x + area.w), float(area.x + area.w), float(area.x)};
        const float ys[8] = {yA, yA, hb, hb, yA, yA, float(bottom + 4), float(bottom + 4)};
        (void)h0;
        const int stageA = std::min(fadeA, int(std::min(t, 300U) * 255U / 300U)) * 150 / 255;
        c.fillPolygon(xs, ys, 8, rgb(3, 4, 10), stageA);
    }

    // the two portraits, sliding in from the sides
    for (int k = 0; k < 2; ++k) {
        const uint32_t since = t >= uint32_t(k) * kSecond ? t - uint32_t(k) * kSecond : 0;
        if (t < uint32_t(k) * kSecond) continue;
        const float in = easeOut(float(since) / float(kSlide));
        const int restX = k == 0 ? area.x + 8 : area.x + area.w - 8 - P;
        const int fromX = k == 0 ? area.x - P - 12 : area.x + area.w + 12;
        const int x = int(float(fromX) + float(restX - fromX) * in);
        const int y = bottom - P;
        const bool lit = k == speaker;
        const uint32_t col = lit ? s_[k].col : mix(s_[k].col, rgb(30, 32, 44), 150);
        const Rect w{x, y, P, P};
        codec::window(c, s_[k].faction, w, col, now, since > kSlide / 2 ? since - kSlide / 2 : 0, fadeA, k * 1500, false);
        if (!lit) c.fillRoundRect(w.x + 6, w.y + 6, w.w - 12, w.h - 12, 4, rgb(4, 5, 12), fadeA * 120 / 255);
        // the role over it: ATTACKER / DEFENDER, with a bar that fills while it talks
        const int tagW = textWidth(FONT_XS, s_[k].tag);
        const int tx = k == 0 ? x : x + P - tagW;
        drawText(c, FONT_XS, tx + 1, y - 13, s_[k].tag, rgb(0, 0, 0), fadeA * 180 / 255);
        drawText(c, FONT_XS, tx, y - 14, s_[k].tag, lit ? mix(s_[k].col, rgb(255, 255, 255), 90) : rgb(120, 126, 150), fadeA);
        if (lit && talking) {  // a small transmission light on the portrait's corner
            const int lx = k == 0 ? x + P - 14 : x + 8, ly = y + 8;
            const int pulse = 130 + int(125.f * (0.5f + 0.5f * std::sin(float(now) / 90.f)));
            c.fillRoundRect(lx, ly, 6, 6, 3, rgb(255, 70, 60), std::min(fadeA, pulse));
        }
    }

    // the speech box under the hex
    if (t + 250U < kStartA || box.w < 80 || box.h < 30) return;
    const int boxA = std::min(fadeA, t < kStartA ? int((t + 250U - kStartA) * 255U / 250U) : 255);
    const uint32_t frameCol = s_[speaker].col;
    c.fillRoundRect(box.x, box.y, box.w, box.h, 7, rgb(6, 8, 20), boxA * 236 / 255);
    {  // a pointer toward the one talking
        const float px = speaker == 0 ? float(box.x) : float(box.x + box.w);
        const float dir = speaker == 0 ? -1.f : 1.f;
        const float my = float(box.y + box.h / 2);
        const float xs[3] = {px, px + dir * 9.f, px};
        const float ys[3] = {my - 8.f, my, my + 8.f};
        c.fillPolygon(xs, ys, 3, rgb(6, 8, 20), boxA * 236 / 255);
        c.fillPolygon(xs, ys, 3, frameCol, boxA * 90 / 255);
    }
    codec::glowFrame(c, box, frameCol, 7, boxA);
    for (int k = 0; k < 2; ++k) {
        int a = 0;
        if (k == 0) a = t + kSwap < startB_ ? 255 : std::max(0, int(startB_ - t) * 255 / int(kSwap));
        else a = t + kSwap < startB_ ? 0 : std::min(255, int(t + kSwap - startB_) * 255 / 220);
        a = a * boxA / 255;
        if (a <= 0 || s_[k].line.empty()) continue;
        const uint32_t nameCol = mix(s_[k].col, rgb(255, 255, 255), 70);
        const std::string name = fitText(FONT_SMB, s_[k].name, box.w - 60);
        drawText(c, FONT_SMB, box.x + 10, box.y + 4, name, nameCol, a);
        const uint32_t from = k == 0 ? kStartA : startB_;
        const bool typing = t >= from && t < from + typeMs(sp_[k]);
        {  // the voice signal beside the name: bars that jump while the words come
            const int x0 = box.x + 10 + textWidth(FONT_SMB, name) + 8, base = box.y + 15;
            const uint32_t tick = now / 80U;
            for (int i = 0; i < 9; ++i) {
                const int h = typing ? 2 + int(bits(tick * 131U + uint32_t(i) * 977U + uint32_t(k)) % 9U) : 2;
                c.fillRect(x0 + i * 3, base - h, 2, h, mix(nameCol, rgb(255, 255, 255), 40), a * 200 / 255);
            }
        }
        const int shown = t >= from ? speech::typed(sp_[k], t - from) : 0;
        speech::draw(c, sp_[k], text.x, text.y, shown, now, a);
        if (typing || (t >= from && (now / 380U) % 2U == 0U && t < from + typeMs(sp_[k]) + 900U)) {
            // the cursor after the last letter typed
            int cxp = text.x, cyp = text.y, lh = 10;
            for (const speech::Word& wd : sp_[k].words) {
                if (wd.start >= shown) break;
                const int vis = std::min(int(wd.text.size()), shown - wd.start);
                cxp = text.x + wd.x + textWidth(*wd.font, wd.text.substr(0, size_t(vis)));
                cyp = text.y + wd.y;
                lh = wd.font->lineHeight;
            }
            c.fillRect(cxp + 1, cyp + 1, 5, std::max(6, lh - 3), nameCol, a * 200 / 255);
        }
    }
}

bool foughtBefore(const ti::Game& g, int a, int b, const std::string& combatText) {
    if (a < 0 || b < 0 || a >= g.numPlayers || b >= g.numPlayers) return false;
    const std::vector<std::string>& log = g.log;
    // this fight's own line (the engine can be a little ahead of what is on screen), then the start of its turn
    size_t cur = log.size();
    for (size_t i = log.size(); i-- > 0;)
        if (log[i] == combatText) {
            cur = i;
            break;
        }
    size_t turn = 0;
    for (size_t i = cur; i-- > 0;) {
        const std::string& l = log[i];
        if (l.size() > 7 && l.compare(l.size() - 7, 7, "'s turn") == 0) {
            turn = i;
            break;
        }
    }
    const std::string na = ti::playerName(g, a), nb = ti::playerName(g, b);
    const std::string keys[4] = {": " + na + " attacks " + nb + ".", ": " + nb + " attacks " + na + ".",
                                 ": " + na + " against " + nb + ".", ": " + nb + " against " + na + "."};
    for (size_t i = 0; i < turn; ++i) {
        const std::string& l = log[i];
        if (l.compare(0, 16, "Space combat in ") != 0 && l.compare(0, 17, "Ground combat on ") != 0) continue;
        for (const std::string& k : keys)
            if (l.size() >= k.size() && l.compare(l.size() - k.size(), k.size(), k) == 0) return true;
    }
    return false;
}

std::string chooseLine(const ti::Game& g, int spk, int lis, bool attacker, bool space, int sys, const std::string& combatText,
                       uint32_t seed, Memory& mem) {
    using speech::Cat;
    if (spk < 0 || lis < 0 || spk >= g.numPlayers || lis >= g.numPlayers) return "";
    const int fs = g.players[size_t(spk)].faction, fl = g.players[size_t(lis)].faction;
    std::set<std::string>& said = mem.said[fs];
    auto take = [&](const std::vector<std::string>& v, uint32_t salt) -> std::string {
        std::vector<std::string> fresh;
        for (const std::string& s : v)
            if (!said.count(s)) fresh.push_back(s);
        const std::vector<std::string>& from = fresh.empty() ? v : fresh;
        if (from.empty()) return "";
        const std::string s = from[bits(seed ^ salt) % uint32_t(from.size())];
        said.insert(s);  // (as written: the same line to someone else counts as said)
        return speech::fillThem(s, fl);
    };
    // the situation, if any: home (the defender's), Mecatol Rex, a rematch
    Cat situ = Cat::Count;
    if (!attacker && sys >= 0 && sys < int(g.systems.size()) && g.systems[size_t(sys)].home == spk) situ = Cat::Home;
    else if (sys >= 0 && sys == g.mecatol()) situ = Cat::Mecatol;
    else if (foughtBefore(g, spk, lis, combatText)) situ = Cat::Rematch;
    if (situ != Cat::Count) {
        // the first time, always; then now and then (its home: 1 in 2, Mecatol Rex 1 in 4, a rematch 1 in 6)
        const std::vector<std::string> v = speech::options(fs, fl, situ, false, false);
        bool fresh = false;
        for (const std::string& s : v) fresh |= !said.count(s);
        const uint32_t again = situ == Cat::Home ? 2U : situ == Cat::Mecatol ? 4U : 6U;
        if (!v.empty() && (fresh || bits(seed ^ 0x5157U) % again == 0U)) return take(v, 0x51U);
    }
    // a line for that rival (2 times in 3)
    const std::vector<std::string> pair = speech::options(fs, fl, attacker ? Cat::Attack : Cat::Defend, true, false);
    if (!pair.empty() && bits(seed ^ 0x9A1EU) % 3U != 2U) return take(pair, 0x9AU);
    // the usual ones
    const Cat kind = attacker ? (space ? Cat::Attack : Cat::Invade) : (space ? Cat::Defend : Cat::Hold);
    return take(speech::options(fs, fl, kind, false, false), 0x77U);
}

}  // namespace combattalk
