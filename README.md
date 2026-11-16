# "Rick Dangerous" for the PlayStation 1

A PSX port of the classic Amiga "Rick Dangerous" by Core Design.

Code is based on **xrick**, BigOrno's C reimplementation of the game.
It is based on Pierluigi Vicinanza's cleaned-up 2014 version (the Rockbox port,
https://github.com/pierluigi-vicinanza/xrick). The game code is unchanged; only a
new platform layer was written for the Sony PlayStation.

## What the port does

| Part | How it works on the PS1 |
|------|-------------------------|
| Video | xrick's 320×200 8-bit frame is uploaded to VRAM each frame as a CLUT texture and drawn with two sprites. The GPU does the palette lookup. Double-buffered, swapped in the vblank interrupt. |
| Timing | A 1 ms clock from root counter 2 (an interrupt every 4234 ticks of system clock/8). The game keeps its original 40 ms frame (25 fps). |
| Sound effects | All 23 effects are converted to SPU ADPCM at build time and uploaded to SPU RAM once (about 357 KB). Each effect has its own hardware voice, so no software mixing is needed. |
| Music | The 6 tunes are CD-DA audio tracks 2–7. Auto-pause detects the end of a track so the title music can loop. Pause and resume continue from the same position. |
| Data | Graphics, maps and texts are packed into a small read-only file table and linked into the executable. |
| Memory | Same stack allocator as the Rockbox port, with a 640 KB static pool. |

### Controls

| Button | Action |
|--------|--------|
| D-pad | Walk, climb, crouch |
| Circle | Jump (same as Up) |
| Cross / Square | Fire. With a direction: Up = shoot, Down = dynamite, Left/Right = poke with stick. |
| Start | Pause / resume |
| Select + Start | End the current game |

## Building

You need:

- a MIPS GCC toolchain: `mipsel-none-elf`, or the Ubuntu/Debian `gcc-mipsel-linux-gnu` package
- [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK)
- CMake, Ninja, and Python 3 with numpy

**If you use the distro `mipsel-linux-gnu` toolchain,** apply `patches/elf2x-pt-load-only.patch` to PSn00bSDK before building it. Otherwise `elf2x` produces a 2 GB executable, because that toolchain adds a `PT_GNU_STACK` program header at address 0.

Build PSn00bSDK:

```sh
cd PSn00bSDK
patch -p1 < /path/to/xrick-psx/patches/elf2x-pt-load-only.patch
cmake --preset default . -DPSN00BSDK_TARGET=mipsel-linux-gnu \
      --install-prefix /opt/psn00bsdk -DSKIP_EXAMPLES=ON
cmake --build build && sudo cmake --install build
```

Build the game. First copy `game/data.zip` from the pierluigi-vicinanza/xrick repository to `assets/data.zip`, then:

```sh
export PATH=/opt/psn00bsdk/bin:$PATH PSN00BSDK_LIBS=/opt/psn00bsdk/lib/libpsn00b
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=$PSN00BSDK_LIBS/cmake/sdk.cmake \
      -DPSN00BSDK_TARGET=mipsel-linux-gnu
cmake --build build
```

The output is `build/xrick.bin` and `build/xrick.cue`: one data track plus 6 audio tracks, about 25 MB.

## Running

- **Emulators:** DuckStation, PCSX-Redux, or RetroArch with the PCSX ReARMed core. Load `xrick.cue`, not the `.bin`, so the music tracks are found.
- **Real hardware:** burn the `.bin`/`.cue` to a CD-R for a modded console, or use an optical drive emulator such as the XStation. Booting the `.exe` alone, for example through Unirom, works but has no music.

## Status

The port was tested in PCSX ReARMed with the HLE BIOS. The splash and title screens, the title music, the high-score screen, the mission intro, walking, jumping, deaths, pause, shooting and dynamite all work at the correct 25 fps. It has not been tested on real hardware yet.

**Known limitations:**

- High scores aren't saved, because memory card support isn't implemented.
- The picture isn't scaled for PAL. It's centred in the frame.

## Files

```
src/xrick/system/*_psx.c   PlayStation platform layer (new)
src/xrick/config.h         build configuration
tools/mkassets.py          data.zip -> romfs.bin, sndbank.bin (SPU ADPCM), CD-DA tracks
CMakeLists.txt, iso.xml, system.cnf
patches/elf2x-pt-load-only.patch
```

## Legal

The xrick code is by BigOrno and Pierluigi Vicinanza, released "in the spirit of the GPL"; see the original README. The graphics, maps and sounds come from the original *Rick Dangerous* by Core Design/Firebird. Rights to them are unclear, so don't distribute disc images built with them.
