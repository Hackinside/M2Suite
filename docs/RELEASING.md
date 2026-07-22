# Releasing

M2Suite ships a **portable package**: unzip anywhere, run
`m2suite-shell.exe`. Everything it needs — the Qt runtime, the Visual C++
runtime, FFmpeg, and all Qt plugins — is inside the folder. No installer, no
admin rights, no registry.

Most users should never have to build this project. That is the point of
shipping the package.

## Where the package lives

| Location | Purpose |
|---|---|
| [Releases](https://github.com/Hackinside/M2Suite/releases) | **Canonical.** Versioned, with changelog notes and a checksum. Link people here. |
| [`dist/M2Suite-portable.zip`](../dist/) | A mirror of the latest release, in the repo, for people who would rather grab one file than navigate a releases page. |

The `dist/` copy is a convenience, not the source of truth. It is replaced
wholesale on each release rather than accumulating versions — git history
holds the old ones if anyone ever needs them, and the repo stays a
reasonable size to clone.

> **Why keep a binary in the repo at all?** Because the alternative — "just
> build it" — means an eight-hour first-time FFmpeg build and a working
> vcpkg + MSVC setup. That is a wall in front of exactly the people this
> tool is for: someone who wants to look inside a game they own.

## Cutting a release

### 1. Confirm the tree is releasable

```bash
cmake --build --preset windows-msvc
ctest --preset windows-msvc --output-on-failure
```

Both must pass. Then open the application and check the file types you
touched against real game data — the test suite cannot verify what game
assets are supposed to look like.

### 2. Update the changelog

Move everything under `## Unreleased` in [CHANGELOG.md](../CHANGELOG.md)
into a new version heading with the date. Keep entries written the way the
existing ones are: **what changed, and what it fixes for the person using
it**. "Fixed AITD framing" is not useful; "models no longer render tiny in
the corner — the camera was framing on the collision volume rather than the
mesh" is.

### 3. Build the package

```powershell
.\build.ps1 -Package
```

This produces `dist/M2Suite-portable/` and zips it. Verify the result on a
machine that is **not** your development box, or at minimum from a shell
with no Qt or MSVC environment loaded — the most common packaging failure is
a DLL that only resolves because your dev environment happens to provide it.

Sanity checks before publishing:

- The folder contains `platforms/qwindows.dll`, `multimedia/`,
  `imageformats/` and `styles/`. Without the platform plugin the
  application dies at startup with *"no Qt platform plugin could be
  initialized"* — see [BUILDING.md](BUILDING.md).
- `README.txt` and `CHANGELOG.txt` inside the package are current.
- The app launches, opens a game folder, and previews at least one file of
  each major family (texture, cel, audio, video, model).

### 4. Tag and publish

```bash
git tag -a v0.10.0 -m "M2Suite v0.10.0"
git push origin main --tags
```

Create the GitHub release from the tag, paste the changelog section as the
release notes, and attach the zip. Publish the SHA-256 alongside it:

```powershell
Get-FileHash dist\M2Suite-portable.zip -Algorithm SHA256
```

### 5. Refresh the in-repo mirror

```bash
git add dist/M2Suite-portable.zip
git commit -m "dist: refresh portable package for v0.10.0"
git push
```

## Versioning

`MAJOR.MINOR.PATCH`, interpreted for a format tool:

- **MAJOR** — the library APIs break, or output formats change
  incompatibly.
- **MINOR** — new formats supported, new features, new export targets.
- **PATCH** — fixes to existing decoders, no new capability.

The **build number** is separate and increments automatically on every
build (`cmake/increment_build.cmake`). It appears in `Help > About` and is
what bug reports should quote, because it identifies the exact binary.

## What every release must carry

- The GPL-3.0 licence text
- `THIRD_PARTY_LICENSES.md`, listing the bundled Qt/FFmpeg/MSVC runtimes
  and their terms
- A pointer to this repository at the matching tag — GPL compliance means
  the corresponding source must be obtainable, and "it's the tag with the
  same name" is the simplest way to guarantee that

Qt is **dynamically linked** and its DLLs sit beside the executable where
they can be replaced, which is what LGPL compliance requires. Do not static
link Qt into a release without revisiting that.

## Never in a release

- Game data of any kind
- SDK source, documentation PDFs, or BIOS/firmware images
- Anything from `Repositories/` or `Documents/`
