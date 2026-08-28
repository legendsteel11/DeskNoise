# DeskNoise

A tray-resident sound generator for Windows. It mixes up to three layers of
tones and noise at once, so you can build a background sound and leave it
running.

한국어 설명은 [README.ko.md](README.ko.md)에 있습니다.

## What it does

- **Five sound sources** — pure tone (sine), narrow-band noise, white noise,
  pink noise and brown noise.
- **Three layers** that play together. Each one carries its own source,
  frequency, bandwidth, volume and stereo balance.
- **A band filter on every noise.** Narrow the band and the noise closes in
  around the chosen frequency; open it fully and the noise keeps its own
  character.
- **Wobble** for pure tones. Two frequencies a fraction apart beat against each
  other, so the tone rises and falls instead of sitting flat.
- **Play and rest cycles.** Play for a set number of minutes, rest, repeat.
- **Presets** for combinations worth keeping.
- **Tray resident**, with `Ctrl+Alt+M` to start and stop from anywhere.
- **Korean and English**, chosen from the Windows display language.

## Requirements

Windows 10 or later. Nothing to install alongside it: DeskNoise is a single
executable linked against the static CRT.

## Building

Visual Studio 2022 with the C++ desktop workload. `cl.exe` is called directly;
there is no project file and no CMake.

```
build.bat         release, writes build\DeskNoise.exe
build.bat debug   debug,   writes build\DeskNoise-debug.exe
```

If Visual Studio sits somewhere other than the default location, change the `VS`
path at the top of `build.bat`.

## Settings

Settings and presets are kept in `%APPDATA%\DeskNoise\config.ini`.

## Command line

| Option | Effect |
| --- | --- |
| `/tray` | Start hidden in the tray. |
| `/lang=ko`, `/lang=en` | Use that language instead of following Windows. |

## Layout of the source

| File | Contents |
| --- | --- |
| `src/main.cpp` | Window, layout, layer editing, presets, tray icon, settings. |
| `src/audio.cpp`, `src/audio.h` | WASAPI shared-mode output and sound generation. |
| `src/i18n.h`, `src/i18n.cpp` | Every UI string, Korean and English side by side. |
| `src/app.rc`, `src/resource.h` | Icon, manifest, version and the preset-name dialog. |

## License

MIT. See [LICENSE](LICENSE).

DeskNoise builds against the Windows SDK and carries no bundled third-party
code. Two well-known algorithms are used as published: the pink-noise filter is
Paul Kellett's refined method from musicdsp.org, and the band-pass coefficients
follow Robert Bristow-Johnson's Audio EQ Cookbook. Both were placed in the
public domain by their authors.
