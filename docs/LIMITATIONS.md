# Limitations and open challenges

What M2Suite does not do yet, and — more usefully — *why*. Each entry names
the actual obstacle, because "not implemented" and "we don't know how" are
very different problems and only one of them is looking for a volunteer.

Categories:

- 🔬 **Blocked on knowledge** — we do not know the format. Needs research.
- 🔧 **Blocked on work** — we know how; nobody has written it.
- 🚫 **Won't fix** — out of scope or the data does not exist.

---

## Alone in the Dark

### 🚫 PolyTexture sampling — no textured geometry exists in the 3DO ports

The most-requested AITD feature, and the answer turns out to be that there
is nothing to sample. A sweep of **every** PAK archive in both 3DO games
found **zero** primitives of type 8, 9 or 10 across 1,095 model bodies. Each
polygon is flat-shaded from the 256-colour palette; the pre-rendered room
backdrops carry all the visual detail.

The UV parsing *is* implemented and tested, so a build that does use
textured primitives (the PC releases, or AITD 3) decodes correctly. But on
3DO there is no texture atlas to sample from, and rendering these polygons
flat is what the reference implementation `fitd` does too.

See [FORMATS.md](FORMATS.md#alone-in-the-dark) for the full sweep results.
**If you find a 3DO AITD body with a type 8/9/10 primitive, that would
disprove this — please open an issue.**

### 🔧 Model animations are not applied

`LISTANIM.PAK` / `5LSTANIM.PAK` hold keyframe animations for the bodies, and
bodies with `INFO_ANIM` carry a bone/group table. The group table is parsed
and skipped; neither the animation data nor skinning is decoded. Models are
shown in their bind pose.

This is a well-understood format with a working reference implementation
(`fitd`) — it is work, not mystery.

### 🔧 Room geometry and camera data are not displayed

`ETAGE00..15.PAK` (floors/rooms) and `MASK00..15.PAK` (camera clipping
masks) are recognised as AITD archives but have no viewer. Rooms are the
more interesting target: they would let a whole location be reconstructed
rather than isolated props.

### 🔧 `.ITD`, `.16X` and `.STK` are recognised but not decoded

- `OBJETS.ITD`, `VARS.ITD`, `DEFINES.ITD`, `PRIORITY.ITD` — game object and
  variable tables
- `*.16X` — in-game book/document pages, magic `PAGES:`
- `*.STK` — string tables

All are small, all are plausibly readable, none is decoded.

### 🔧 Exported OBJ carries no UVs

`exportAitdBodyObj` writes vertices, faces and per-colour materials. Since
no 3DO model has UVs there is nothing to write today, but the exporter
should emit `vt` records when it is handed a body that has them.

---

## Textures

### 🔧 UTF: only PIP-indexed 8-bit textures decode

`decodeLodToRgba` handles PIP-indexed, uncompressed, 8-bits-per-index
textures. RLE-compressed texel data and direct (non-palette) texel formats
throw `NotImplementedError` rather than guess.

The SDK Mercury texture library source is available and is the ground truth
for both — this is transcription work, not research.

---

## Images

### 🔬 PEBM stores no dimensions

The PEBM container (Oldsmobile) parses cleanly, but **nowhere in it is a
width or a height**. Without dimensions the pixel buffer cannot be laid out,
and a wrong guess produces a diagonally-sheared image rather than an
obviously wrong one.

Ways this could be solved: a companion file on the same disc that carries
the dimensions; a sample whose dimensions are known from a screenshot; or
finding the code in the game executable that sets up the blit. The
disassembler is in the box, so the third route is open to anyone.

### 🔧 One `OVERFACE.DAT` frame renders incorrectly

Frame 15 of 74 in Street Fighter's `DEM/OVERFACE.DAT` (96×112, 6 bpp,
multi-CCB) decodes wrongly while the other 73 are correct. Deferred as
cosmetic — but a single bad frame among 73 good ones usually means one
specific packed-row edge case, so it is probably a small, findable bug.

---

## Audio

### 🔧 Compressed AIFC `sowt` is not supported

Byte-swapped PCM. Straightforward; nobody has needed it yet.

### 🔧 RSRC bundles expose only their first sound

Road Rash's `Rash.AIFF` is a resource bundle of 19 AIFF sounds. M2Suite
unwraps it to the first one. The resource table is fully parsed, so exposing
all 19 in the frame selector is small work.

---

## Video

### 🔧 MP4 export needs a standalone `ffmpeg.exe`

Cinepak → MP4 export and MPEG remuxing shell out to `ffmpeg.exe` on `PATH`
or at `C:\ffmpeg\bin\`. Without it, video export falls back to numbered PNG
frames. The bundled FFmpeg *libraries* handle playback, so this only affects
export.

---

## Discs and archives

### 🚫 `.chd` disc images

Convert to `.bin`/`.cue` or `.iso` first. Supporting CHD means vendoring a
substantial dependency for a format that is a conversion away.

### 🔬 Gex "bigfile" entries are proprietary

The Crystal Dynamics bigfile archive structure is understood and entries
extract cleanly. The **entries themselves** are game-internal formats with
no magic bytes and no public description. QuickBMS — the usual last resort —
ships no Gex script; its repository is source-only for this title.

Reverse-engineering these would start from the Gex executable, which
M2Suite can disassemble.

---

## Platform

### 🚫 Windows only

The format libraries are portable C++20 with no platform dependency; the Qt
shell uses only cross-platform Qt APIs. A Linux or macOS build is plausible
and has simply never been attempted. The build system, the vcpkg triplet
pinning and the plugin deployment rules are all Windows-specific and would
need work.

---

## The general challenges

Recurring difficulties worth naming, because they shape how this project
works.

**Undocumented formats with no error detection.** Almost none of these
formats carry checksums or version fields. A wrong offset does not fail —
it produces *plausible* output. The AITD vertex-offset bug is the canonical
example: a heuristic found a field that "fit" and produced geometry that
looked almost right, and it was wrong for weeks. The defence is to validate
against something external: a bounding box, an exact file size, a reference
implementation's output, or a statistical property of the data.

**Published documentation is sometimes wrong.** FFmpeg's `segafilm` demuxer
treats 3DO FILM audio as raw signed PCM; it is SDX2, and the difference is
the difference between audio and noise. Trust decoded output over
specifications.

**Test data cannot be redistributed.** Every fixture that matters is
copyrighted game data. This is why tests synthesise their inputs, and why
bug reports must describe files rather than attach them.

**One tool, many games, no common ancestry.** Each game studio built its own
containers on top of the same handful of 3DO primitives. Detection has to be
structural and defensive, and every new game is a chance for a
previously-solid assumption to break.
