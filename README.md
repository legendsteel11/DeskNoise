# DeskNoise

A sound generator for Windows that runs in the system tray. It mixes up to
three layers of tones and noise, so you can build a background sound and leave
it running.

한국어 설명은 [README.ko.md](README.ko.md)에 있습니다.

## What it does

- **Five sound sources** — pure tone (sine), narrow-band noise, white noise,
  pink noise and brown noise.
- **Three layers** that play together. Each has its own source, frequency,
  bandwidth, volume and stereo balance.
- **A band filter on every noise source.** Narrow the band and the noise closes
  in around the chosen frequency; open it fully and the noise keeps its own
  character.
- **Wobble** for pure tones. Two frequencies a fraction apart beat against each
  other, so the tone rises and falls instead of sitting flat.
- **Play continuously or on a cycle.** Run without stopping, or play for a set
  number of minutes, leave a gap, and repeat. The volume can fade out over the
  end of each play period.
- **Presets** for combinations worth keeping.
- **Runs in the tray** — closing the window leaves it playing.
- **Korean and English**, following the Windows display language.

## Keys

`Ctrl+Alt+M` works whether or not the window is open. The rest work while the
window has focus.

| Key | Action |
| --- | --- |
| `Space` | Start and stop. |
| `+`, `-` | Master volume, 5% per step. |
| `Ctrl+Alt+M` | Start and stop from any application. |

## Requirements

Windows 10 or later. There is no runtime to install: DeskNoise is a single
executable linked against the static CRT.

## Building

Visual Studio 2022 with the C++ desktop workload. `cl.exe` is called directly;
there is no project file and no CMake.

```
build.bat         release, writes build\DeskNoise.exe
build.bat debug   debug,   writes build\DeskNoise-debug.exe
```

If Visual Studio is installed somewhere other than the default location, change
the `VS` path at the top of `build.bat`.

## Settings

Settings and presets are kept in `%APPDATA%\DeskNoise\config.ini`.

## Command line

| Option | Effect |
| --- | --- |
| `/tray` | Start hidden in the tray. |
| `/lang=ko`, `/lang=en` | Use the given language instead of following Windows. |

## Source layout

| File | Contents |
| --- | --- |
| `src/main.cpp` | Window, layout, layer editing, presets, tray icon, settings. |
| `src/audio.h`, `src/audio.cpp` | WASAPI shared-mode output and sound generation. |
| `src/i18n.h`, `src/i18n.cpp` | Every UI string, Korean and English side by side. |
| `src/app.rc`, `src/resource.h` | Icon, manifest, version and the preset-name dialog. |

## Other tools by the same maker

- [TabStick](https://tabstick.com/), sticky index notes that attach to
  windows.
- [Edgetree](https://edgetree.vercel.app/), a VS Code explorer-style file
  browser docked to the screen edge.
- [SweepCap](https://sweepcap.vercel.app/), a capture utility where the drag
  itself is the capture.

## License

MIT. See [LICENSE](LICENSE).

DeskNoise builds against the Windows SDK and bundles no third-party code. Two
well-known algorithms are used as published: the pink-noise filter is Paul
Kellett's refined method from musicdsp.org, and the band-pass coefficients
follow Robert Bristow-Johnson's Audio EQ Cookbook. Both were placed in the
public domain by their authors.
