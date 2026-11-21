// The player profile - see profile.h.
#include "game/profile.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include "core/log.h"
#include "game/leagues.h"
#include "ui/pcolors.h"

namespace profile {
namespace {
std::string g_path = "profile.cfg";
std::map<std::string, std::string> g_kv;

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string x;
    while (std::getline(ss, x, ','))
        if (!x.empty()) v.push_back(x);
    return v;
}
std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
}  // namespace

void load(const std::string& path) {
    g_path = std::getenv("TI4_PROFILE") ? std::getenv("TI4_PROFILE") : path;
    g_kv.clear();
    std::ifstream in(g_path);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq != std::string::npos) g_kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    LOG("profile: %zu values from %s (%zu leagues finished)", g_kv.size(), g_path.c_str(), finishedLeagues().size());
}
bool save() {
    std::ofstream out(g_path, std::ios::binary);
    if (!out) {
        LOG("profile: could not write %s", g_path.c_str());
        return false;
    }
    out << "# Fallen Imperium - your profile: leagues finished, the league in progress, what the rivals remember\n";
    for (const auto& kv : g_kv) out << kv.first << "=" << kv.second << "\n";
    return true;
}

bool unlockAll() { return getInt("unlock_all") != 0; }
void setUnlockAll(bool on) { setInt("unlock_all", on ? 1 : 0); }

std::vector<std::string> finishedLeagues() { return split(get("finished")); }
bool finished(const std::string& lg) {
    for (const std::string& f : finishedLeagues())
        if (f == lg) return true;
    return false;
}
void markFinished(const std::string& lg) {
    if (finished(lg)) return;
    const std::string f = get("finished");
    set("finished", f.empty() ? lg : f + "," + lg);
}
bool leagueOpen(const std::string& lg) {
    const league::LeagueDef* d = league::find(lg);
    if (!d) return false;
    if (unlockAll() || !d->after || !*d->after) return true;
    return finished(d->after);
}
std::vector<std::pair<std::string, uint32_t>> earnedEmblems() {
    std::vector<std::pair<std::string, uint32_t>> v;
    if (const char* e = std::getenv("TI4_DRAFT_EMBLEMS")) {  // (test only: the title screen with medals)
        for (const std::string& k : split(e))
            if (const league::LeagueDef* d = league::find(k))
                if (d->emblem && *d->emblem) v.push_back({d->emblem, d->color});
        return v;
    }
    for (const std::string& lg : finishedLeagues())
        if (const league::LeagueDef* d = league::find(lg))
            if (d->emblem && *d->emblem) v.push_back({d->emblem, d->color});
    return v;
}
bool colourUnlocked(int c) {
    if (c < pcolours::kClassic || unlockAll()) return true;
    for (const league::LeagueDef* d : league::all())
        if (d->colour == c) return finished(d->id);
    return true;  // (a colour no league gives stays open)
}
std::string colourSource(int c) {
    for (const league::LeagueDef* d : league::all())
        if (d->colour == c) return std::string(std::string(d->name).compare(0, 4, "The ") == 0 ? "Finish " : "Finish the ") + d->name;
    return "";
}

std::string get(const std::string& key, const std::string& def) {
    auto it = g_kv.find(key);
    return it == g_kv.end() ? def : it->second;
}
int getInt(const std::string& key, int def) {
    auto it = g_kv.find(key);
    return it == g_kv.end() || it->second.empty() ? def : std::atoi(it->second.c_str());
}
void set(const std::string& key, const std::string& value) { g_kv[key] = value; }
void setInt(const std::string& key, int v) { g_kv[key] = std::to_string(v); }
void erase(const std::string& prefix) {
    for (auto it = g_kv.begin(); it != g_kv.end();)
        if (it->first.compare(0, prefix.size(), prefix) == 0) it = g_kv.erase(it);
        else ++it;
}

}  // namespace profile
