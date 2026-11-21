#include "ui/speech.h"

#include <cctype>
#include <cmath>
#include <cstring>

#include "engine/data.h"

using namespace ti;

namespace speech {
namespace {

struct SpeechName {
    const char* key;
    const char* name;
};
struct SpeechLine {
    const char* key;
    const char* to;  // a pair line: only to this faction
    int cat;
    const char* text;
};
#include "ui/gen/lines.inc"

const char* keyOf(int f) { return f >= 0 && f < kFactionCount ? kFactions[f].key : ""; }

uint32_t mixh(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t k = s.find(from); k != std::string::npos; k = s.find(from, k + to.size())) s.replace(k, from.size(), to);
}

// a small generator for the machine junk (the same junk for the same line)
struct Gen {
    uint32_t s;
    uint32_t next() { return s = s * 1664525U + 1013904223U, mixh(s); }
    int below(int n) { return int(next() % uint32_t(n)); }
};

struct Piece {
    std::string text;
    const FontData* font;
    uint32_t col;
    uint8_t fx;
    bool space;    // a space before it
    bool newline;  // starts a new line
};

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string w;
    for (char ch : s) {
        if (ch == ' ') {
            if (!w.empty()) out.push_back(w);
            w.clear();
        } else {
            w += ch;
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// machine junk: digits and letters in small dim type, glued to runs of Lazaxian glyphs in the accent colour
void junk(std::vector<Piece>& out, Gen& g, uint32_t accent, uint32_t dim, int groups, bool spaceFirst) {
    static const char kAsc[] = "zZ0123456789TtsSkx";
    static const char kLaz[] = "abcdefghijklmnopqrstuvwxyzKRTQZ";
    for (int k = 0; k < groups; ++k) {
        std::string a, l;
        for (int n = 2 + g.below(4); n > 0; --n) a += kAsc[g.below(int(sizeof(kAsc)) - 1)];
        for (int n = 1 + g.below(3); n > 0; --n) l += kLaz[g.below(int(sizeof(kLaz)) - 1)];
        out.push_back({a, &FONT_SM, dim, 5, k == 0 && spaceFirst, false});
        out.push_back({l, &FONT_LAZ, accent, 5, false, false});
    }
}

}  // namespace

uint32_t seedOf(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return mixh(a * 0x9E3779B1U ^ mixh(b + 0x632BE5ABU) ^ mixh(c * 31U + 7U) ^ mixh(d + 0x85EBCA77U));
}

uint32_t seedOf(const std::string& s, uint32_t salt) {
    uint32_t h = 2166136261U;
    for (unsigned char ch : s) h = (h ^ ch) * 16777619U;
    return mixh(h ^ salt);
}

std::string address(int faction) {
    const char* k = keyOf(faction);
    for (const SpeechName& n : kSpeechNames)
        if (!std::strcmp(n.key, k)) return n.name;
    return faction >= 0 && faction < kFactionCount ? factionShortName(faction) : "stranger";
}

std::string pick(int speaker, int listener, Cat cat, uint32_t seed) {
    const char* sk = keyOf(speaker);
    const char* lk = keyOf(listener);
    if (!*sk) return "";
    auto gather = [&](Cat c, std::vector<const char*>& pair, std::vector<const char*>& gen) {
        for (const SpeechLine& l : kSpeechLines) {
            if (l.cat != int(c) || std::strcmp(l.key, sk)) continue;
            if (!l.to) gen.push_back(l.text);
            else if (*lk && !std::strcmp(l.to, lk)) pair.push_back(l.text);
        }
    };
    std::vector<const char*> pair, gen;
    gather(cat, pair, gen);
    if (pair.empty() && gen.empty()) {  // (every faction has every kind; this is only a safety net)
        Cat alt = cat == Cat::Counter || cat == Cat::Yield ? Cat::Decline : Cat::Offer;
        gather(alt, pair, gen);
    }
    if (pair.empty() && gen.empty()) return "";
    const char* chosen = !pair.empty() && (seed % 3U != 2U || gen.empty()) ? pair[(seed / 3U) % pair.size()]
                                                                         : gen[(seed / 7U) % gen.size()];
    std::string s = chosen;
    replaceAll(s, "{them}", listener >= 0 ? address(listener) : std::string("stranger"));
    return s;
}

std::string fillThem(const std::string& text, int listener) {
    std::string s = text;
    replaceAll(s, "{them}", listener >= 0 ? address(listener) : std::string("stranger"));
    return s;
}

std::vector<std::string> options(int speaker, int listener, Cat cat, bool pairOnly, bool fill) {
    std::vector<std::string> out;
    const char* sk = keyOf(speaker);
    const char* lk = keyOf(listener);
    if (!*sk) return out;
    for (const SpeechLine& l : kSpeechLines) {
        if (l.cat != int(cat) || std::strcmp(l.key, sk)) continue;
        if (pairOnly ? !(l.to && *lk && !std::strcmp(l.to, lk)) : l.to != nullptr) continue;
        out.push_back(fill ? fillThem(l.text, listener) : std::string(l.text));
    }
    return out;
}

Speech layout(int speaker, const std::string& text, int width, const FontData& font, uint32_t accent, uint32_t textCol,
              uint32_t seed) {
    const std::string key = keyOf(speaker);
    const uint32_t dim = mix(textCol, rgb(70, 76, 96), 150);
    std::vector<Piece> ps;
    Gen g{seed | 1U};
    auto plain = [&](const std::string& s, const FontData* f, uint32_t col, uint8_t fx, bool firstNewline) {
        bool first = true;
        for (const std::string& w : words(s)) {
            ps.push_back({w, f, col, fx, !ps.empty() && !(first && firstNewline), first && firstNewline && !ps.empty()});
            first = false;
        }
    };
    if (key == "l1z1x") {  // owner: the Lazax machine talk - junk with Lazaxian glyphs at both ends
        junk(ps, g, accent, dim, 2, false);
        ps.push_back({"...", &font, dim, 5, false, false});
        const size_t from = ps.size();
        plain(text, &font, textCol, 0, false);
        for (size_t k = from; k < ps.size(); ++k) {  // (owner 2026-11-18) "Lazax" glitches: its a's flicker into 1s and back (and "L1Z1X"'s 1s into a's)
            std::string low;
            for (char ch : ps[k].text)
                if (std::isalnum(static_cast<unsigned char>(ch))) low += char(std::tolower(static_cast<unsigned char>(ch)));
            if (low.rfind("lazax", 0) == 0 || low.rfind("l1z1x", 0) == 0) ps[k].fx = 6;
        }
        ps.push_back({"...", &font, dim, 5, true, false});
        junk(ps, g, accent, dim, 2, false);
    } else if (key == "nekro") {  // only two words readable in every line
        const size_t bar = text.find('|');
        const std::string a = trim(bar == std::string::npos ? text : text.substr(0, bar));
        const std::string b = trim(bar == std::string::npos ? std::string() : text.substr(bar + 1));
        junk(ps, g, accent, dim, 2, false);
        ps.push_back({"...", &font, dim, 5, false, false});
        ps.push_back({a, &font, textCol, 0, false, false});
        ps.push_back({"...", &font, dim, 5, false, false});
        junk(ps, g, accent, dim, 2, false);
        ps.push_back({"...", &font, dim, 5, false, false});
        if (!b.empty()) ps.push_back({b, &font, textCol, 0, false, false});
        ps.push_back({"...", &font, dim, 5, false, false});
        junk(ps, g, accent, dim, 1, false);
    } else if (key == "obsidian") {  // the owner: all their lines in the Lazaxian alphabet
        plain(text, &FONT_LAZ, textCol, 0, false);
    } else if (key == "ghemina") {  // the Lord leads, the Lady finishes
        const size_t bar = text.find("||");
        const std::string lord = trim(bar == std::string::npos ? text : text.substr(0, bar));
        const std::string lady = trim(bar == std::string::npos ? std::string() : text.substr(bar + 2));
        ps.push_back({"LORD", &FONT_SMB, accent, 0, false, false});
        plain(lord, &font, textCol, 0, false);
        if (!lady.empty()) {
            ps.push_back({"LADY", &FONT_SMB, mix(accent, rgb(255, 150, 200), 140), 0, false, true});
            plain(lady, &font, mix(textCol, rgb(255, 214, 230), 90), 0, false);
        }
    } else if (key == "zelian") {  // the growl grinds, the [translation] reads plainly
        const size_t br = text.find('[');
        const std::string growl = trim(br == std::string::npos ? text : text.substr(0, br));
        plain(growl, &font, rgb(176, 150, 120), 3, false);
        if (br != std::string::npos) plain(text.substr(br), &font, textCol, 0, false);
    } else {
        const uint8_t fx = key == "mortheus" ? 1 : key == "arborec" ? 2 : (key == "ghost" || key == "crimson") ? 4 : 0;
        plain(text, &font, textCol, fx, false);
    }

    // flow the pieces into lines
    Speech sp;
    sp.width = width;
    sp.font = &font;
    struct LineInfo {
        size_t first, last;
        int asc, h;
    };
    std::vector<LineInfo> lines;
    int x = 0;
    size_t lineStart = 0;
    std::vector<int> xs(ps.size(), 0);
    std::vector<int> lineOf(ps.size(), 0);
    for (size_t i = 0; i < ps.size(); ++i) {
        const Piece& p = ps[i];
        const int w = textWidth(*p.font, p.text);
        const int gap = p.space ? textWidth(*p.font, " ") : 0;
        if (i > lineStart && (p.newline || x + gap + w > width)) {
            lines.push_back({lineStart, i - 1, 0, 0});
            lineStart = i;
            x = 0;
        } else {
            x += gap;
        }
        xs[i] = x;
        lineOf[i] = int(lines.size());
        x += w;
    }
    if (!ps.empty()) lines.push_back({lineStart, ps.size() - 1, 0, 0});
    for (LineInfo& L : lines) {
        for (size_t i = L.first; i <= L.last; ++i) {
            L.asc = std::max(L.asc, ps[i].font->ascent);
            L.h = std::max(L.h, ps[i].font->lineHeight);
        }
        L.h = std::max(L.h, font.lineHeight);
        L.asc = std::max(L.asc, font.ascent);
    }
    int y = 0, chars = 0;
    for (const LineInfo& L : lines) {
        for (size_t i = L.first; i <= L.last; ++i) {
            Word w;
            w.text = ps[i].text;
            w.font = ps[i].font;
            w.col = ps[i].col;
            w.fx = ps[i].fx;
            w.x = xs[i];
            w.y = y + L.asc - ps[i].font->ascent;
            w.start = chars;
            chars += int(w.text.size()) + 1;
            sp.words.push_back(w);
        }
        y += L.h;
    }
    sp.height = y;
    sp.chars = chars;
    return sp;
}

Speech layoutFit(int speaker, const std::string& text, int width, int maxH, uint32_t accent, uint32_t textCol, uint32_t seed) {
    Speech s;
    for (const FontData* f : {&FONT_MD, &FONT_SM, &FONT_XS}) {
        s = layout(speaker, text, width, *f, accent, textCol, seed);
        if (s.height <= maxH) break;
    }
    return s;
}

void draw(Canvas& c, const Speech& s, int x, int y, int shown, uint32_t now, int alpha) {
    const float t = float(now);
    int i = 0;
    for (const Word& w : s.words) {
        ++i;
        const int len = int(w.text.size());
        const int vis = shown < 0 ? len : std::max(0, std::min(len, shown - w.start));
        if (vis <= 0) continue;
        int a = alpha, dx = 0, dy = 0;
        switch (w.fx) {
            case 1: a = int(float(alpha) * (0.3f + 0.7f * (0.5f + 0.5f * std::sin(t / 430.f + float(w.start) * 0.21f)))); break;
            case 2: dy = int(std::lround(1.1f * std::sin(t / 520.f + float(i) * 0.9f))); break;
            case 3: dx = int((now / 70U + uint32_t(i) * 3U) % 3U) - 1, dy = int((now / 90U + uint32_t(i)) % 2U); break;
            case 4: a = int(float(alpha) * (0.55f + 0.45f * (0.5f + 0.5f * std::sin(t / 650.f + float(i) * 1.3f)))); break;
            case 5: a = ((now / 90U + uint32_t(i)) % 5U) == 0 ? alpha * 2 / 5 : alpha; break;
            case 6: {  // the L1Z1X's "Lazax": a's and 1s swap now and then, letter by letter, in a glitch colour
                const std::string t = vis < len ? w.text.substr(0, size_t(vis)) : w.text;
                bool lower = false;
                for (char ch : w.text) lower |= ch >= 'a' && ch <= 'z';
                int cx = x + w.x;
                const uint32_t slot = now / 110U;
                for (size_t j = 0; j < t.size(); ++j) {
                    const char ch = t[j];
                    char out = ch;
                    bool on = false;
                    if (ch == 'a' || ch == 'A' || ch == '1') {
                        on = mixh(slot * 2654435761U ^ uint32_t(i) * 97U ^ uint32_t(j) * 131U ^ 0x5A5AU) % 100U < 38U;
                        if (on) out = ch == '1' ? (lower ? 'a' : 'A') : '1';
                    }
                    drawText(c, *w.font, cx + (on ? int(slot % 2U) : 0), y + w.y, std::string(1, out), on ? mix(w.col, rgb(255, 70, 90), 170) : w.col, alpha);
                    cx += textWidth(*w.font, std::string(1, ch));
                }
                continue;
            }
            default: break;
        }
        drawText(c, *w.font, x + w.x + dx, y + w.y + dy, vis < len ? w.text.substr(0, size_t(vis)) : w.text, w.col, a);
    }
}

int typed(const Speech& s, uint32_t ms) { return std::min(s.chars, int(ms * 55U / 1000U)); }

}  // namespace speech
