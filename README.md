<img width="2272" height="1888" alt="M2Suite_Logo" src="https://github.com/user-attachments/assets/91dad2ee-d302-4e2c-b8c4-8910b9b0afcf" />

# M2Suite

**Converter & visualizer for 3DO and Panasonic M2 file formats.**

Point M2Suite at an unpacked game folder — or hand it a disc image — and it
identifies, previews, plays and converts what it finds. It is a native
Windows application (C++20 / Qt 6) with no runtime dependency on emulators
or the original SDK.

| | |
|---|---|
| **Platform** | Windows 10/11 x64 |
| **Language** | C++20, Qt 6 (Widgets + Multimedia) |
| **Build** | CMake + vcpkg manifest mode, MSVC |
| **Licence** | See [Licensing](#licensing) — not yet chosen for the original code |

---

## What it reads

| Family | Formats | State |
|---|---|---|
| **Textures** | M2 UTF / M2TX — paged, palettised (PIP), multi-LOD | Preview + PNG export |
| **Images** | 3DO cels (CCB/PDAT/PLUT, 1–16 bpp, packed & coded), IMAG screens, M1 VRAM (LRForm) buffers | Preview + PNG export |
| **Animation** | ANIM files, multi-frame cel chains | Looped playback + per-frame PNG |
| **Video** | DataStreamer movies (Cinepak), standalone FILM, MPEG-1 | Playback with audio, MP4/PNG export |
| **Audio** | AIFF/AIFC with SDX2, SQS2, CBD2, ADP4 (IMA ADPCM); WAV; RSRC bundles | Playback + WAV export |
| **Executables** | M2 PowerPC ELF, incl. LZSS-compressed sections | Disassembly + pseudocode |
| **Discs** | ISO / BIN / IMG via the Opera filesystem | Direct extraction |
| **Alone in the Dark** | PAK archives, 3D bodies, pre-rendered backdrops | Interactive 3D viewport, OBJ/MTL + PNG export |

Everything M2Suite knows about these formats — including the parts that
contradict published documentation — is written down in
**[docs/FORMATS.md](docs/FORMATS.md)**. That file is the real product of
this project; the application is what proves the findings work.

## Documentation

| Document | What is in it |
|---|---|
| [docs/FORMATS.md](docs/FORMATS.md) | Every format finding, byte layouts, and the traps that cost the most time |
| [docs/BUILDING.md](docs/BUILDING.md) | Toolchain requirements and the exact build steps |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the libraries and the shell fit together |
| [docs/LIMITATIONS.md](docs/LIMITATIONS.md) | What does not work yet, and the open challenge behind each gap |
| [docs/REFERENCES.md](docs/REFERENCES.md) | Every source this work builds on, and how to propose a new one |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Bug reports, feature requests, format findings, code |
| [CHANGELOG.md](CHANGELOG.md) | Release history |

## Quick start

### Run a release build

Download the portable package from the Releases page, unzip it anywhere and
run `m2suite-shell.exe`. The Qt runtime, the Visual C++ runtime, FFmpeg and
all Qt plugins ship inside the folder — no install, no admin rights.

### Build from source

```bash
cmake --preset windows-msvc && cmake --build --preset windows-msvc
```

Full prerequisites (MSVC toolset pinning, vcpkg setup, the multi-hour
one-time FFmpeg build) are in [docs/BUILDING.md](docs/BUILDING.md). If you
only want the format libraries and their tests, the `core-msvc` preset needs
no Qt and configures in seconds:

```bash
cmake --preset core-msvc && cmake --build --preset core-msvc && ctest --preset core-msvc
```

### First steps in the app

- `File > Open Folder` — browse an unpacked game
- `File > Extract Disc Image` — unpack an `.iso` / `.bin` / `.img`
- `File > Convert Images to UTF` — turn PNG/JPG into M2 textures
- `File > Export Selected` — batch-convert to PNG / WAV / MP4 / OBJ
- `Help > About` — credits

Select a file to preview it. Video and animation start automatically.
**Copy Info** puts the file details and full path on the clipboard, which is
exactly what a good bug report needs.

A run log is written to `m2suite.log` beside the executable — check it first
if something misbehaves.

## Contributing

Bug reports, format findings and feature requests are all welcome, and the
rules for each are in **[CONTRIBUTING.md](CONTRIBUTING.md)**. Two things
matter more than anything else:

1. **Name the file.** A format bug that cannot be reproduced against a
   specific file cannot be fixed. Give the game, the exact path inside it,
   and the file size.
2. **Never attach copyrighted game data.** Describe it, hex-dump the first
   64 bytes, quote the error — but do not upload the asset itself.

Have a source we should be reading — a spec, a decompilation, an
open-source tool? Open a **Reference source** issue; see
[docs/REFERENCES.md](docs/REFERENCES.md) for what makes a good one.

## Licensing

> **⚠ Publishing this repository is not yet cleared.**
> `libs/libm2core` and `libs/libm2texture` are ported from 3DO M2 SDK source
> that carries an explicit confidentiality notice. The 3DO Company dissolved
> in 2003 and the disposition of its IP has not been researched or resolved.
> **Read [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) before making
> this repository public or cutting a release.**

The original M2Suite code has **no licence chosen yet** — until one is
added, treat it as all-rights-reserved and open an issue if you need
specific terms.

Third-party code vendored into this tree keeps its own licence; every piece
is listed with its terms in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

M2Suite ships no game data. 3DO, Panasonic M2, and every game named in this
repository are the property of their respective owners; the formats are
documented here for preservation and interoperability.
