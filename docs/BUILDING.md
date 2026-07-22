# Building M2Suite

M2Suite targets **Windows 10/11 x64** and builds with **MSVC + CMake +
vcpkg** in manifest mode. There is no supported Linux or macOS build today;
the format libraries are portable C++20 and the shell is the only part tied
to Windows, so a port is plausible but has not been attempted.

## Requirements

| Component | Version | Notes |
|---|---|---|
| Visual Studio 2022 | Community or higher, **toolset 14.4x** | See the toolset warning below |
| CMake | 3.21+ | The VS-bundled one works: `Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` |
| Ninja | any recent | Ships with Visual Studio |
| vcpkg | current | `VCPKG_ROOT` must be set as an environment variable |
| Git | any | For vcpkg and for cloning |

Dependencies are declared in `vcpkg.json` and fetched automatically. The
significant ones are **Qt 6** (Widgets, Multimedia, MultimediaWidgets) and
**FFmpeg**, which arrives as a qtmultimedia dependency.

> **Budget several hours for the first configure.** FFmpeg alone takes on
> the order of eight hours to build from source on a typical desktop. This
> is a one-time cost — vcpkg's binary cache means subsequent configures take
> seconds. Do not interrupt it and assume the build is hung.

### Toolset pinning — read this before you file a build bug

vcpkg will happily select a newer Visual Studio instance than the one you
intend, and this is a known failure mode for this project: with VS 18
BuildTools preview toolset **14.51**, `cl.exe` **crashes with 0xC0000005**
while compiling `qtbase`. It is a compiler crash, not a code error, and no
amount of rebuilding helps.

`cmake/triplets/x64-windows.cmake` pins the instance to avoid this. If you
edit it, note that `VCPKG_VISUAL_STUDIO_PATH` **must use backslashes** —
vcpkg does a literal string comparison and will silently fail to match a
path written with forward slashes, falling back to whatever instance it
prefers.

## Presets

Two presets are defined in `CMakePresets.json`.

### `core-msvc` — libraries and tests only

No Qt, no FFmpeg, configures in seconds. This is what you want for format
work, and what CI should use.

```bash
cmake --preset core-msvc
cmake --build --preset core-msvc
ctest --preset core-msvc
```

### `windows-msvc` — the full GUI

Run from a **Visual Studio Developer prompt** (or after sourcing
`vcvars64.bat`), with `VCPKG_ROOT` set.

```bash
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

The executable lands at
`build\windows-msvc\apps\m2suite-shell\m2suite-shell.exe`, with the Qt DLLs
and plugins deployed beside it.

`build.ps1` in the repository root wraps the above for convenience.

## Things the build does that may surprise you

**Qt plugins are deployed by hand.** vcpkg's `applocal` step copies Qt's
core DLLs next to the executable but *not* Qt plugins, and vcpkg's qtbase
does not ship `windeployqt`. Without `platforms/qwindows.dll` the
application dies at startup with *"no Qt platform plugin could be
initialized"*. `apps/m2suite-shell/CMakeLists.txt` therefore copies the
platform plugin, the multimedia backends, the image-format plugins and the
modern Windows style plugin explicitly, plus the transitive DLLs those
plugins load at runtime (ffmpeg's `av*`/`sw*`, and `jpeg62.dll` for
`qjpeg`). If you add a Qt plugin dependency, add its deployment rule too.

**The build number auto-increments.** A custom target with no output is
always considered out of date, so `cmake/increment_build.cmake` runs on
every build, bumping `build_number.txt` and regenerating `BuildInfo.h` in
the build tree.

**`.c` files are not compiled.** The root `project()` call declares
`LANGUAGES CXX` only. A `.c` file added to a target is listed as `<None>` in
the generated project and **silently never compiled** — you find out at link
time with an unresolved external, and the cause is not obvious from the
error. This bit us with the vendored PKWARE `explode` decoder. If you vendor
C code, either rename it to `.cpp` and wrap it in `extern "C" { ... }` (what
this tree does, see `libs/libm2model/third_party/pak_explode.cpp`) or enable
the C language in `project()`.

## Optional runtime dependency: ffmpeg.exe

Two features shell out to a standalone `ffmpeg.exe` if one is on `PATH` or
at `C:\ffmpeg\bin\ffmpeg.exe`:

- remuxing MPEG elementary streams for smooth in-app playback with audio
- exporting Cinepak video to `.mp4`

Everything else works without it; MPEG *playback* uses the bundled FFmpeg
libraries.

## Verifying a build

```bash
ctest --preset windows-msvc --output-on-failure
```

All tests are self-contained — they synthesise their own fixtures or use
small checked-in ones, so **no game data is required**. `libm2model_tests`
is the one exception: it is a manual tool that takes a real PAK path as an
argument, so it is built but deliberately not registered with ctest.

Then launch the executable and open a game folder. If something misbehaves,
`m2suite.log` beside the executable records what was opened, the playback
backend status, and any error.
