# Emulador TK90X / TK95 - build Linux (padrao) e Windows (cruzado, llvm-mingw)
#
#   make              -> build/tk90x            (Linux)
#   make windows      -> build/win/tk90x.exe    (Windows x86_64, estatico, sem DLL)
#   make test         -> testes rapidos (Z80 Fuse, fita, snapshot, maquina)
#   make test-zex     -> ZEXDOC + ZEXALL (demora alguns minutos)
#   make android      -> build/android/tk90x-<versao>-{debug,release}.apk (kit em tools/android)
#   make clean

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra -Wno-unused-parameter -D_DEFAULT_SOURCE

# SDL2: usa sdl2-config se existir; senao os headers locais em tools/sdl2-linux
ifneq ($(shell command -v sdl2-config 2>/dev/null),)
  SDL_CFLAGS := $(shell sdl2-config --cflags)
  SDL_LIBS   := $(shell sdl2-config --libs)
else
  SDL_CFLAGS := -Itools/sdl2-linux/include -Itools/sdl2-linux/include/SDL2 -D_REENTRANT
  SDL_LIBS   := -Ltools/sdl2-linux/lib -lSDL2 -Wl,-rpath,/usr/lib/x86_64-linux-gnu
endif

CORE_SRC = src/z80.c src/tape.c src/snapshot.c src/machine.c src/roms_embedded.c src/profiles.c
APP_SRC  = $(CORE_SRC) src/png.c src/platform.c src/osd.c src/touch.c src/main.c

BUILD = build

all: $(BUILD)/tk90x

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/tk90x: $(APP_SRC) src/*.h | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(APP_SRC) $(SDL_LIBS) -lm

# ---------------- Windows (cruzado) ----------------
WIN_CC   ?= tools/llvm-mingw/bin/x86_64-w64-mingw32-gcc
SDLW     ?= tools/sdl2-mingw/x86_64-w64-mingw32
WIN_CFLAGS = -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter -I$(SDLW)/include/SDL2 -Dmain=SDL_main
WIN_LIBS   = -s -L$(SDLW)/lib -static -lmingw32 -lSDL2main -lSDL2 -mwindows \
             -ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 -lwinmm -limm32 -lole32 \
             -loleaut32 -lshell32 -lsetupapi -lversion -luuid -lm

windows: $(BUILD)/win/tk90x.exe

$(BUILD)/win/tk90x.exe: $(APP_SRC) src/*.h
	mkdir -p $(BUILD)/win
	$(WIN_CC) $(WIN_CFLAGS) -o $@ $(APP_SRC) $(WIN_LIBS)
	cp -r roms $(BUILD)/win/ 2>/dev/null || true

# ---------------- Testes ----------------
TEST_CFLAGS = $(CFLAGS) -Isrc -g

$(BUILD)/z80_fusetest: tests/z80_fusetest.c src/z80.c src/z80.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -o $@ tests/z80_fusetest.c src/z80.c

$(BUILD)/zextest: tests/zextest.c src/z80.c src/z80.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -o $@ tests/zextest.c src/z80.c

$(BUILD)/tape_test: tests/tape_test.c src/tape.c src/tape.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -o $@ tests/tape_test.c src/tape.c -lm

# Mesmo teste com AddressSanitizer + UBSan (fuzz sem leitura fora dos limites)
$(BUILD)/tape_test_asan: tests/tape_test.c src/tape.c src/tape.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -fsanitize=address,undefined -fno-sanitize-recover=all \
	    -fno-omit-frame-pointer -o $@ tests/tape_test.c src/tape.c -lm

test-tape-asan: $(BUILD)/tape_test_asan
	$(BUILD)/tape_test_asan tests/data

# snapshot_test inclui src/snapshot.c (para testar a compressao interna)
$(BUILD)/snapshot_test: tests/snapshot_test.c src/snapshot.c src/tape.c src/snapshot.h src/tape.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -o $@ tests/snapshot_test.c src/tape.c -lm

$(BUILD)/machine_test: tests/machine_test.c $(CORE_SRC) src/png.c src/*.h | $(BUILD)
	$(CC) $(TEST_CFLAGS) -O2 -o $@ tests/machine_test.c $(CORE_SRC) src/png.c -lm

test: $(BUILD)/z80_fusetest $(BUILD)/tape_test $(BUILD)/snapshot_test $(BUILD)/machine_test
	$(BUILD)/z80_fusetest tests/data/tests.in tests/data/tests.expected
	$(BUILD)/tape_test tests/data
	$(BUILD)/snapshot_test tests/data
	$(BUILD)/machine_test tests/data $(BUILD)/test-out

test-zex: $(BUILD)/zextest
	$(BUILD)/zextest tests/data/zexdoc.com
	$(BUILD)/zextest tests/data/zexall.com

# ---------------- Android (APK) ----------------
# Usa o kit de tools/android (env.sh) e o projeto Gradle de android/ (ver docs/ANDROID.md).
# Saida: $(BUILD)/android/tk90x-<versao>-debug.apk e -release.apk
android:
	android/build.sh $(abspath $(BUILD))/android

clean:
	rm -rf $(BUILD)

.PHONY: all windows test test-zex test-tape-asan clean android
