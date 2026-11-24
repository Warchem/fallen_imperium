// Where the player's own files live (0.85.1, owner 2026-11-24: "Whenever the game gets updated, myself, as well as others, lose
// our league process. A profile folder would be great to have so we can migrate our league progress and unlocks between
// versions."). The profile (profile.cfg: leagues, unlocks, rivals' memory), the settings (settings.cfg: options, keys, the bots'
// memory of your habits) and the autosave (savegame.ti4) go into one folder outside the game's own folder, so every version
// finds them:
//   Windows             <Saved Games>\Fallen Imperium   (C:\Users\<you>\Saved Games\Fallen Imperium)
//   Linux / Steam Deck  $XDG_DATA_HOME/Fallen Imperium, else ~/.local/share/Fallen Imperium
//   the Miyoo, or a file named portable.txt next to the game: the game's own folder, as before 0.85.1
// The first start that finds that folder without a profile or settings brings them in from the old place: the game's own
// folder, or the newest copy in the folders beside it (an older version unzipped next to this one) - see init().
#pragma once
#include <string>

namespace paths {

void init();                              // main(), before the settings and the profile are read (not for the headless tests)
const std::string& userDir();             // "" = the game's own folder; else the folder, ending with a separator
std::string user(const std::string& file);  // where that file of the player's lives
std::string shownDir();                   // the folder for the screen ("the game's folder" when it is that)
const std::string& importedFrom();        // the folder the first start brought old files in from ("" = none)
int importedCount();                      // how many files
bool openFolder();                        // Windows: opens the folder in Explorer (elsewhere: false)

}  // namespace paths
