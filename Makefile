# Twilight Imperium - Miyoo Mini Plus homebrew
#
#   make toolchain   download the ARM cross-compiler (Linux x86-64 only, ~300 MB)
#   make miyoo       build the Miyoo program        -> build/miyoo/ti4
#   make package     build the SD-card zip           -> dist/TI4-Miyoo-<version>.zip
#   make host        build a PC version for testing  -> build/host/ti4
#   make windows     build the Windows version       -> build/win/TwilightImperium.exe
#   make windows-package  the Windows zip            -> dist/TI4-Windows-<version>.zip
#   make windows-toolchain  download llvm-mingw for the Windows build (~90 MB)
#   make test        run the scripted screen tests   -> build/test/*.png
#   make font        regenerate the embedded fonts (needs Python + Pillow)
#   make icons       repack assets/icons/**.png into assets/icons.dat (Python + Pillow)
#   make retro       rebake the Retrowave tileset art -> assets/retro.dat + the sharp PC art assets/retro-pc.dat
#                    (Python + numpy, ~1 + ~5 min)
#   make voidfall    paint the Voidfall tileset -> assets/voidfall-pc.dat / -miyoo.dat (numpy + scipy, ~1 min)
#   make art         campaign backdrops -> assets/art/ (Python + numpy + Pillow; in git)
#   make portraits   faction codec portraits -> assets/art/portraits-pc.png (tools/portraits; in git)
#   make lines       negotiation lines tools/lines/*.txt -> src/ui/gen/lines.inc (in git)
#   make music       re-encode the owner's music tracks -> assets/music/ (ffmpeg; not in git; *_SRC=...)

VERSION   := $(shell sed -n 's/^\#define TI4_VERSION "\(.*\)"/\1/p' src/app/app.h)
MUSIC_SRC ?= /mnt/user-data/uploads/Action_Space_Opera_TTRPG_Music.mp3
INFIL_SRC ?= /mnt/user-data/uploads/Infiltration_Space_Opera_TTRPG_Music.mp3
TITLE_SRC ?= assets/music_src/The_Council_Keleres_-_Embers_of_the_Imperium.mp3
# 0.30.0: the three tracks under the names the game looks for next to it
COPY_MUSIC = for t in title:"The Council Keleres - Embers of the Imperium" action:"Action - Twilight Symposium" infiltration:"Infiltration - Embers of the Imperium"; do \
	  k=$${t%%:*}; n=$${t\#*:}; if [ -f assets/music/$$k-$(1).mp3 ]; then cp assets/music/$$k-$(1).mp3 "$(2)/$$n.mp3"; else echo "WARNING: no $$k music (make music)"; fi; done
TOOLCHAIN ?= $(HOME)/.ti4-toolchain/x64-gcc-10.3.0-glibc-2.28/arm-rpi-linux-gnueabihf
CROSS     ?= $(TOOLCHAIN)/bin/arm-rpi-linux-gnueabihf-
WINTOOLS  ?= $(HOME)/.ti4-toolchain/llvm-mingw-20240619-ucrt-ubuntu-20.04-x86_64
WINCROSS  ?= $(WINTOOLS)/bin/x86_64-w64-mingw32-

SRCS      := $(shell find src -name '*.cpp' | sort)
COMMON    := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc -Ithird_party -Ithird_party/SDL12 -MMD -MP
ARMFLAGS  := -marm -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard

HOST_OBJS  := $(SRCS:src/%.cpp=build/host/obj/%.o)
MIYOO_OBJS := $(SRCS:src/%.cpp=build/miyoo/obj/%.o)
WIN_SRCS   := $(filter-out src/platform/fbdev.cpp src/platform/sdl12.cpp,$(SRCS))
WIN_OBJS   := $(WIN_SRCS:src/%.cpp=build/win/obj/%.o)
VERSION_NUM := $(shell echo $(VERSION) | tr '.' ',' ),0

APPDIR    := TwilightImperium
PKGROOT   := build/package
ZIP       := dist/TI4-Miyoo-$(VERSION).zip

.PHONY: art portraits lines all host miyoo package windows windows-package windows-toolchain test font icons retro voidfall music linux linux-package toolchain clean

all: host

# ---- PC build (for automated tests) -------------------------------------
host: build/host/ti4
build/host/ti4: $(HOST_OBJS)
	g++ $(HOST_OBJS) -o $@ -ldl -lm -lpthread
build/host/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	g++ $(COMMON) -g -c $< -o $@

# ---- Miyoo build ------------------------------------------------------------
miyoo: build/miyoo/ti4
build/miyoo/ti4: $(MIYOO_OBJS)
	$(CROSS)g++ $(ARMFLAGS) $(MIYOO_OBJS) -o $@ -static-libstdc++ -static-libgcc -ldl -lm -lpthread -s -Wl,-Map=build/miyoo/ti4.map
build/miyoo/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CROSS)g++ $(COMMON) $(ARMFLAGS) -c $< -o $@

package: build/miyoo/ti4
	@max=$$($(CROSS)readelf -V build/miyoo/ti4 | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1); \
	  if [ "$$max" != "GLIBC_2.4" ]; then echo "ERROR: Miyoo build needs $$max (must be GLIBC_2.4 only)"; exit 1; fi
	rm -rf $(PKGROOT) && mkdir -p $(PKGROOT)/App/$(APPDIR) dist
	cp build/miyoo/ti4 package/launch.sh package/config.json package/icon.png $(PKGROOT)/App/$(APPDIR)/
	cp package/README.txt package/RULES-CHECK.txt assets/icons.dat assets/retro.dat assets/voidfall-miyoo.dat $(PKGROOT)/App/$(APPDIR)/
	@$(call COPY_MUSIC,miyoo,$(PKGROOT)/App/$(APPDIR))
	mkdir -p $(PKGROOT)/App/$(APPDIR)/art && cp assets/art/eve_of_the_fall-miyoo.jpg assets/art/fated_expedition-miyoo.jpg assets/art/trade_wars-miyoo.jpg assets/art/twilights_fall-miyoo.jpg assets/art/action_back-miyoo.png assets/art/codec_space-miyoo.jpg assets/art/codec_alert-miyoo.jpg $(PKGROOT)/App/$(APPDIR)/art/
	python3 tools/portraits/split_strips.py assets/art/portraits-pc.png src/ui/gen/portrait_keys.inc $(PKGROOT)/App/$(APPDIR)/art/portraits  # (0.58.0: one strip per faction)
	chmod +x $(PKGROOT)/App/$(APPDIR)/ti4 $(PKGROOT)/App/$(APPDIR)/launch.sh
	rm -f $(ZIP) && cd $(PKGROOT) && python3 -c "import shutil; shutil.make_archive('../../dist/TI4-Miyoo-$(VERSION)', 'zip', '.')"
	cp build/miyoo/ti4.map dist/TI4-Miyoo-$(VERSION).map
	@echo "Built $(ZIP)"

# ---- Linux desktop / Steam Deck build (SDL2 opened at runtime) ---------------
# One x86-64 binary; libstdc++ / libgcc linked in. SDL2 comes from the system
# (SteamOS has it). Owner, 2026-10-24: a one-off build to test on his Steam Deck.
LINUX_SRCS := $(filter-out src/platform/win32.cpp,$(SRCS))
LINUX_OBJS := $(LINUX_SRCS:src/%.cpp=build/linux/obj/%.o)
linux: build/linux/ti4
build/linux/ti4: $(LINUX_OBJS)
	g++ $(LINUX_OBJS) -o $@ -static-libstdc++ -static-libgcc -ldl -lm -lpthread -s -Wl,-Map=build/linux/ti4.map
build/linux/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	g++ $(COMMON) -DTI4_SDL2 -Ithird_party/SDL2 -c $< -o $@
linux-package: build/linux/ti4
	@# 0.58.3: refuse a binary that needs a glibc newer than 2.35 (SteamOS 3.5) - add a stand-in to glibc_compat.cpp
	@bad=$$(objdump -T build/linux/ti4 | grep -oE 'GLIBC_2\.(3[6-9]|[4-9][0-9])' | sort -u); \
	  if [ -n "$$bad" ]; then echo "build/linux/ti4 needs $$bad:"; objdump -T build/linux/ti4 | grep -E 'GLIBC_2\.(3[6-9]|[4-9][0-9])'; exit 1; fi
	rm -rf build/linuxpkg && mkdir -p build/linuxpkg/TwilightImperium dist
	cp build/linux/ti4 assets/icons.dat assets/retro.dat assets/retro-pc.dat assets/voidfall-pc.dat package/linux/TwilightImperium.sh build/linuxpkg/TwilightImperium/
	@$(call COPY_MUSIC,pc,build/linuxpkg/TwilightImperium)
	mkdir -p build/linuxpkg/TwilightImperium/art && cp assets/art/eve_of_the_fall-pc.jpg assets/art/fated_expedition-pc.jpg assets/art/trade_wars-pc.jpg assets/art/twilights_fall-pc.jpg assets/art/action_back-pc.png assets/art/portraits-pc.png assets/art/codec_space-pc.jpg assets/art/codec_alert-pc.jpg build/linuxpkg/TwilightImperium/art/
	cp package/linux/README-SteamDeck.txt build/linuxpkg/TwilightImperium/README.txt
	cp package/README.txt build/linuxpkg/TwilightImperium/GUIDE.txt
	chmod +x build/linuxpkg/TwilightImperium/ti4 build/linuxpkg/TwilightImperium/TwilightImperium.sh
	rm -f dist/TI4-SteamDeck-$(VERSION).tar.gz && tar -czf dist/TI4-SteamDeck-$(VERSION).tar.gz -C build/linuxpkg TwilightImperium
	cp build/linux/ti4.map dist/TI4-SteamDeck-$(VERSION).map
	@echo "Built dist/TI4-SteamDeck-$(VERSION).tar.gz"

# ---- Windows build (cross-compiled with llvm-mingw) ---------------------------
# One .exe with everything linked in: it only needs Windows 10 or 11.
windows: build/win/TwilightImperium.exe
build/win/TwilightImperium.exe: $(WIN_OBJS) build/win/obj/resources.o
	$(WINCROSS)g++ $^ -o $@ -static -mwindows -s -Wl,--stack,8388608 -Wl,-Map=build/win/TwilightImperium.map -lgdi32 -luser32 -lwinmm
build/win/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(WINCROSS)g++ $(COMMON) -c $< -o $@
build/win/obj/resources.o: package/windows/ti4.rc package/windows/ti4.ico src/app/app.h
	@mkdir -p $(dir $@)
	printf '#define TI4_VER_NUM %s\n#define TI4_VER_STR "%s"\n' "$(VERSION_NUM)" "$(VERSION)" > build/win/obj/version.h
	$(WINCROSS)windres -I package/windows -I build/win/obj $< -O coff -o $@

windows-package: build/win/TwilightImperium.exe
	rm -rf build/winpkg && mkdir -p build/winpkg/TwilightImperium dist
	cp build/win/TwilightImperium.exe assets/icons.dat assets/retro.dat assets/retro-pc.dat assets/voidfall-pc.dat build/winpkg/TwilightImperium/
	@$(call COPY_MUSIC,pc,build/winpkg/TwilightImperium)
	mkdir -p build/winpkg/TwilightImperium/art && cp assets/art/eve_of_the_fall-pc.jpg assets/art/fated_expedition-pc.jpg assets/art/trade_wars-pc.jpg assets/art/twilights_fall-pc.jpg assets/art/action_back-pc.png assets/art/portraits-pc.png assets/art/codec_space-pc.jpg assets/art/codec_alert-pc.jpg build/winpkg/TwilightImperium/art/
	cp package/windows/README-Windows.txt build/winpkg/TwilightImperium/README.txt
	cp package/README.txt build/winpkg/TwilightImperium/GUIDE.txt
	cd build/winpkg/TwilightImperium && for f in README.txt GUIDE.txt; do sed -i 's/$$/\r/' $$f; done
	rm -f dist/TI4-Windows-$(VERSION).zip && cd build/winpkg && python3 -c "import shutil; shutil.make_archive('../../dist/TI4-Windows-$(VERSION)', 'zip', '.')"
	cp build/win/TwilightImperium.map dist/TI4-Windows-$(VERSION).map  # (0.43.3: names the addresses in a crash report)
	@echo "Built dist/TI4-Windows-$(VERSION).zip"

windows-toolchain:
	sh tools/get-windows-toolchain.sh

# ---- Tests --------------------------------------------------------------------
test: build/host/ti4
	@rm -rf build/test && mkdir -p build/test
	@./build/host/ti4 --selftest
	@for s in tests/*.txt; do \
	  n=$$(basename $$s .txt); mkdir -p build/test/$$n; \
	  ./build/host/ti4 --headless $$s build/test/$$n > build/test/$$n/stdout.txt 2>&1 \
	    || { echo "FAILED: $$s"; tail -5 build/test/$$n/stdout.txt; exit 1; }; \
	  echo "ok   $$s"; \
	done
	@mkdir -p build/test/ui && ./build/host/ti4 --uitest build/test/ui > build/test/ui/stdout.txt 2>&1 \
	  || { echo "FAILED: ui screens"; tail -5 build/test/ui/stdout.txt; exit 1; }; echo "ok   decision screens"
	@mkdir -p build/test/audio && ./build/host/ti4 --audiotest build/test/audio > build/test/audio/stdout.txt 2>&1 \
	  || { echo "FAILED: sounds"; tail -5 build/test/audio/stdout.txt; exit 1; }; echo "ok   sounds"
	@mkdir -p build/test/hd && TI4_SCALE=2.25 ./build/host/ti4 --headless tests/01_tour.txt build/test/hd > build/test/hd/stdout.txt 2>&1 \
	  || { echo "FAILED: sharp picture"; tail -5 build/test/hd/stdout.txt; exit 1; }; echo "ok   sharp picture (1440x1080)"
	@mkdir -p build/test/waypoints && TI4_UITEST_ONLY=waypoints ./build/host/ti4 --uitest build/test/waypoints > build/test/waypoints/stdout.txt 2>&1 \
	  || { echo "FAILED: waypoints"; tail -5 build/test/waypoints/stdout.txt; exit 1; }; echo "ok   waypoints (routes, Waypoint Mode screens)"
	@mkdir -p build/test/codec && TI4_UITEST_ONLY=codec ./build/host/ti4 --uitest build/test/codec > build/test/codec/stdout.txt 2>&1 \
	  || { echo "FAILED: codec"; tail -5 build/test/codec/stdout.txt; exit 1; }; echo "ok   codec (every faction's portrait and voice)"
	@TI4_SIMWAYPOINTS=1 ./build/host/ti4 --simulate 8 6 77 2>/dev/null | grep -E "waypoints|simulated"
	@./build/host/ti4 --simulate 30 0 1 2>/dev/null | tail -1
	@python3 tools/ppm2png.py build/test

font:
	python3 tools/gen_font.py src/gfx/font_data.inc

icons:
	python3 tools/gen_icons.py assets/icons assets/icons.dat

art:
	python3 tools/gen_campaign_art.py docs/owner assets/art
	python3 tools/gen_card_backs.py docs/owner assets/art

# the faction codec portraits (0.57.0): every animated portrait -> one atlas + the key list
portraits:
	python3 tools/portraits/bake_atlas.py assets/art/portraits-pc.png src/ui/gen/portrait_keys.inc

# the negotiation lines (0.57.0): tools/lines/*.txt -> src/ui/gen/lines.inc
lines:
	python3 tools/gen_lines.py tools/lines src/ui/gen/lines.inc

music:
	mkdir -p assets/music
	ffmpeg -hide_banner -loglevel error -y -i "$(MUSIC_SRC)" -map 0:a -map_metadata -1 -ac 1 -ar 44100 -c:a libmp3lame -b:a 64k assets/music/action-miyoo.mp3
	ffmpeg -hide_banner -loglevel error -y -i "$(MUSIC_SRC)" -map 0:a -map_metadata -1 -ac 2 -ar 44100 -c:a libmp3lame -b:a 96k assets/music/action-pc.mp3
	ffmpeg -hide_banner -loglevel error -y -i "$(INFIL_SRC)" -map 0:a -map_metadata -1 -ac 1 -ar 44100 -c:a libmp3lame -b:a 64k assets/music/infiltration-miyoo.mp3
	ffmpeg -hide_banner -loglevel error -y -i "$(INFIL_SRC)" -map 0:a -map_metadata -1 -ac 2 -ar 44100 -c:a libmp3lame -b:a 96k assets/music/infiltration-pc.mp3
	ffmpeg -hide_banner -loglevel error -y -i "$(TITLE_SRC)" -map 0:a -map_metadata -1 -ac 1 -ar 44100 -c:a libmp3lame -b:a 64k assets/music/title-miyoo.mp3
	ffmpeg -hide_banner -loglevel error -y -i "$(TITLE_SRC)" -map 0:a -map_metadata -1 -ac 2 -ar 44100 -c:a libmp3lame -b:a 128k assets/music/title-pc.mp3

retro: build/host/ti4
	./build/host/ti4 --dumpdata > build/retro_data.json
	python3 tools/gen_retro.py build/retro_data.json assets/retro.dat
	python3 tools/gen_retro.py build/retro_data.json assets/retro-pc.dat --scale 2

voidfall: build/host/ti4
	./build/host/ti4 --dumpdata > build/retro_data.json
	python3 tools/gen_voidfall.py build/retro_data.json assets

toolchain:
	sh tools/get-toolchain.sh

clean:
	rm -rf build dist

-include $(HOST_OBJS:.o=.d) $(MIYOO_OBJS:.o=.d) $(WIN_OBJS:.o=.d) $(LINUX_OBJS:.o=.d)
