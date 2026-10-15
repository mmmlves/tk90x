# Windows cross-build from Linux (no sudo)

Status: verified 2026-10-14 on this box (Ubuntu 24.04-class host, x86_64). Everything lives
under the project's `tools/` directory; nothing is installed system-wide.

## 1. What is installed

| Component | Version | Path |
|---|---|---|
| llvm-mingw (UCRT, Linux x86_64 host) | release `20261006`, clang 23.1.3 | `/home/mmalves/workspace/emuladores/tk90x/tools/llvm-mingw/` |
| SDL2 MinGW development package | 2.32.10 (latest 2.x) | `/home/mmalves/workspace/emuladores/tk90x/tools/sdl2-mingw/` |

Sources:

- https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64.tar.xz
  (84 003 044 bytes, sha256 `5f9c6ed95b2d4bdb2869a488c5fd5857fbdabcf288a0aa3eb1da43f6a08d8ab4`).
  The extracted dir `llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64/` was renamed to `llvm-mingw/` (618 MB).
- https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz
  (13 892 870 bytes, sha256 `83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2`).
  The extracted dir `SDL2-2.32.10/` was renamed to `sdl2-mingw/` (89 MB).

Relevant layout:

```
tools/llvm-mingw/bin/x86_64-w64-mingw32-gcc      # clang wrapper, GCC-style name
tools/llvm-mingw/bin/x86_64-w64-mingw32-clang    # same compiler
tools/llvm-mingw/bin/x86_64-w64-mingw32-strip / -objdump / -windres / -ar ...
tools/sdl2-mingw/x86_64-w64-mingw32/include/SDL2/SDL.h ...
tools/sdl2-mingw/x86_64-w64-mingw32/lib/libSDL2.a        # static
tools/sdl2-mingw/x86_64-w64-mingw32/lib/libSDL2.dll.a    # import lib for SDL2.dll
tools/sdl2-mingw/x86_64-w64-mingw32/lib/libSDL2main.a
tools/sdl2-mingw/x86_64-w64-mingw32/bin/SDL2.dll         # ship next to the .exe for dynamic builds
tools/sdl2-mingw/x86_64-w64-mingw32/bin/sdl2-config      # works with --prefix=
tools/sdl2-mingw/i686-w64-mingw32/...                    # 32-bit, unused
```

The Linux SDL2 setup (`tools/sdl2-linux/`) is separate and was not touched.

To re-create from scratch (no sudo needed):

```sh
T=/home/mmalves/workspace/emuladores/tk90x/tools
D=$(mktemp -d)
curl -L -o $D/llvm.tar.xz https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64.tar.xz
curl -L -o $D/sdl.tar.gz  https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz
tar -xJf $D/llvm.tar.xz -C $T && mv $T/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64 $T/llvm-mingw
tar -xzf $D/sdl.tar.gz  -C $T && mv $T/SDL2-2.32.10 $T/sdl2-mingw
```

## 2. Variables used below

```sh
CC=/home/mmalves/workspace/emuladores/tk90x/tools/llvm-mingw/bin/x86_64-w64-mingw32-gcc
SDLW=/home/mmalves/workspace/emuladores/tk90x/tools/sdl2-mingw/x86_64-w64-mingw32
SDL_CFLAGS="-I$SDLW/include/SDL2 -Dmain=SDL_main"
SDL_WINLIBS="-ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid"
```

`SDL_CFLAGS` and `SDL_WINLIBS` are exactly what `$SDLW/lib/pkgconfig/sdl2.pc` (lines 13-15)
declares as `Cflags` and `Libs.private`. Source code includes `<SDL.h>` and must define
`int main(int argc, char *argv[])` (SDL2main renames it to `SDL_main`).

## 3. Build variants (all four verified)

Test program: a window + accelerated renderer + 320x240 ARGB8888 streaming texture + 2 s
event loop + `SDL_Init(VIDEO|AUDIO|TIMER)`. Compiled with `-std=c99 -O2 -Wall -Wextra`
with zero warnings.

### 3a. Dynamic SDL2, console subsystem (debugging; printf visible)

```sh
$CC -std=c99 -O2 -Wall -Wextra $SDL_CFLAGS test.c -o test.exe \
    -L$SDLW/lib -lmingw32 -lSDL2main -lSDL2
```
`file test.exe` -> `PE32+ executable (console) x86-64, for MS Windows` (80 384 bytes).
Needs `SDL2.dll` (copy `$SDLW/bin/SDL2.dll`) beside the exe.

### 3b. Dynamic SDL2, GUI subsystem (no console window)

Same as 3a plus `-mwindows`.
`file` -> `PE32+ executable (GUI) x86-64, for MS Windows` (80 384 bytes). Needs `SDL2.dll`.

### 3c. Static SDL2, GUI -- single self-contained .exe (RECOMMENDED for release)

```sh
$CC -std=c99 -O2 -s -Wall -Wextra $SDL_CFLAGS test.c -o tk90x.exe \
    -L$SDLW/lib -static -lmingw32 -lSDL2main -lSDL2 -mwindows $SDL_WINLIBS
```
`file` -> `PE32+ executable (GUI) x86-64, for MS Windows`.
Size: 12 861 952 bytes without `-s` (almost all DWARF debug sections), 1 738 752 bytes
stripped (`-s` at link time, or `x86_64-w64-mingw32-strip`).

`-static` is what makes the linker choose `libSDL2.a` over `libSDL2.dll.a`. Equivalent
alternative without `-static`: name the archive by full path, which is what
`sdl2-config --static-libs` emits:

```sh
$CC -std=c99 -O2 -s $($SDLW/bin/sdl2-config --prefix=$SDLW --cflags) test.c -o tk90x.exe \
    $($SDLW/bin/sdl2-config --prefix=$SDLW --static-libs)
# expands to: -L$SDLW/lib -lmingw32 -lSDL2main $SDLW/lib/libSDL2.a -mwindows
#   -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va -lm $SDL_WINLIBS
```
(also verified, with `x86_64-w64-mingw32-clang` and `-Werror`; 1 738 240 bytes).
Note `--prefix=` is required: the script otherwise derives the prefix from its own location,
which happens to be correct here, but passing it is explicit and robust.

### 3d. Static SDL2, console subsystem

3c without `-mwindows` -> `PE32+ executable (console)`. Useful for a debug build that prints
to stdout/stderr while being self-contained.

## 4. Runtime dependencies of the static exe

`x86_64-w64-mingw32-objdump -p tk90x.exe | grep 'DLL Name'` lists only OS DLLs:

```
KERNEL32 USER32 GDI32 SHELL32 ADVAPI32 WINMM SETUPAPI OLEAUT32 ole32 IMM32 VERSION
api-ms-win-crt-{stdio,runtime,locale,heap,private,string,math,environment,convert,utility,filesystem}-l1-1-0
```

No `SDL2.dll`, no `libunwind.dll`/`libc++.dll` (pure C, so llvm-mingw's C++ runtime is not
pulled in). The `api-ms-win-crt-*` imports are the Universal CRT: built into Windows 10/11;
on Windows 7/8.1 they require the UCRT update (KB2999226) or the VC++ 2015+ redistributable.
If Windows 7 without updates ever matters, use the `msvcrt` flavour of llvm-mingw
(`llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64.tar.xz`, same release) instead -- not needed now.

## 5. Optional extras

- Icon / version info: write a `.rc` file and compile with
  `tools/llvm-mingw/bin/x86_64-w64-mingw32-windres app.rc -O coff -o app.res.o`, then add
  `app.res.o` to the link line.
- Makefile convention suggestion: `make CC=$CC TARGET=windows` choosing `EXE=.exe`,
  `SDL_CFLAGS`/`SDL_LIBS` from section 2/3c.

## 6. Testing the .exe: wine is NOT available

Checked: `command -v wine wine64` -> nothing; no `*wine*` in `/usr/bin`, `/usr/local/bin`,
`/opt`, `~/.local/bin`; no flatpak/snap wine; `find / -xdev -maxdepth 6 -iname 'wine*'`
finds no executables; no wine dpkg package; no wine binfmt_misc entry; no `~/.wine`.

So the produced executables were validated statically only (`file`, `objdump -p` import
tables, clean link). They must be smoke-tested on a real Windows machine.

Possible no-sudo option, NOT done (out of scope, unverified on this host): the portable
prebuilt Wine tarballs from https://github.com/Kron4ek/Wine-Builds (latest `11.19`,
`wine-11.19-amd64-wow64.tar.xz`, ~99 MB) unpack anywhere and the `wow64` variant needs only
64-bit host libraries; run as `<dir>/bin/wine tk90x.exe`.
