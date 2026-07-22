# Changelog

## Unreleased

### Alone in the Dark — models now render correctly

- **Bone groups are resolved.** This was the big one. An animated AITD body
  stores its vertices in *group-local* space — every vertex in a group is an
  offset from that group's base vertex — and we were reading them as if they
  were model space. Limbs stayed bunched around the origin, so characters
  rendered as a jumble of overlapping shards. `LISTBOD2.PAK` entry 12
  (Emily Hartwood) now matches a reference render of the same body.
  See [docs/FORMATS.md](docs/FORMATS.md#bone-groups--vertices-are-not-in-model-space).
- **Faithful colours.** AITD has no lighting model — the artists baked
  shading into their choice of palette index per face. The added light term
  double-shaded everything, and with a two-sided winding test it dimmed most
  of the mesh. "Solid (materials)" now renders the palette colour
  unmodified; "Solid (flat)" keeps the shading, where reading form is the
  point.
- Bone/group count is shown in the info pane, and the parsed group table is
  exposed for the animation work that comes next.

### Alone in the Dark — browsing

- **Entry names from community databases.** If an AITD_PakEdit
  `*PAK_DB.json` sits beside the archive (or one level up), entries are
  labelled with real names — "12 — Emily Hartwood" rather than "Model 12".
  The databases are read when present and never bundled.
- **Fixed a file-association bug**: exported models sitting in a game folder
  (`LISTBOD2.PAK_00000.ply`) were reported as AITD PAK archives, because the
  filename *contains* a known archive name. Detection is now structural —
  the offset table is validated first, and the name is only a shortcut past
  the content probe.
- New types recognised: `.16X` in-game documents (`PAGES:` magic), `.ITD`
  engine tables, and `.obj`/`.ply`/`.mtl`/`.gltf` export output, so a game
  folder someone has been converting in no longer looks half-broken.

### Project

- **Licensed under GPL-3.0-or-later.** M2Suite now carries an explicit
  licence, so contributors know where they stand and the project can reuse
  GPL-compatible preservation code (including the MAME-derived PowerPC
  disassembler, if it is brought back in).
- **Provenance and disclaimer written down.**
  [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) now records where every
  piece came from — the 3DO M2 SDK, the public-domain PKWARE decoder, the
  AITD reference implementations, and the Qt/FFmpeg runtime bundled at
  release — alongside a plain statement that this is a non-commercial
  preservation project shipping no game data, no SDK and no BIOS images.
- **The portable package ships in the repository**
  ([`dist/M2Suite-portable.zip`](dist/M2Suite-portable.zip)) as well as in
  Releases, so nobody needs a working MSVC + vcpkg setup — and an eight-hour
  first FFmpeg build — just to open a game they own.
- `build.ps1 -Package` stages and zips the portable build, using an
  allow-list of file types so build litter (a 25 MB `.pdb`, CMake install
  scripts) cannot leak into a release.
- Added [docs/RELEASING.md](docs/RELEASING.md),
  [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md), a pull-request template, and CI
  that builds the format libraries, runs the tests, and fails the build if
  anything resembling game data or an unexpected large file is committed.

### Alone in the Dark — 3D viewport rewritten

- **Interactive camera.** The model view is now a proper viewport:
  left-drag orbits, right/middle-drag pans, the wheel zooms, double-click
  resets, and the idle spin pauses while you are dragging.
- **Models are centred and correctly framed.** Two separate bugs caused the
  "model is tiny in the corner" and "model is cut off at the bottom"
  reports:
  - the camera was framing on the model's *declared bounding box*, which
    doubles as a collision volume and can be an order of magnitude larger
    than the mesh;
  - the fit included vertices no primitive references (bone roots and
    animation helpers, usually at the origin), which dragged the centre
    downward.
  Framing now uses the extents of *drawn* geometry only, scaled by the
  bounding-sphere radius so the model keeps a constant size and never clips
  as the camera orbits.
- **Per-pixel depth buffer.** Interpenetrating faces — limbs crossing a
  torso — no longer tear. Depth sorting is kept as a cheap first pass.
- **Wireframe mode** added alongside Solid (materials), Solid (flat) and
  Points.

### Alone in the Dark — PolyTexture primitives

- **Fixed a parse desynchronisation.** PolyTexture types 9 and 10 append a
  `(u,v)` byte pair per point, inline in the primitive stream. These were
  not being consumed, so every primitive *after* one of them decoded as
  garbage — the visible symptom was long thin spikes shooting out of
  affected models. UVs are now parsed and exposed on `AitdPrimitive`.
- **Established that the 3DO ports contain no textured geometry.** A sweep
  of every PAK in both games found zero type 8/9/10 primitives across 1,095
  model bodies. Every polygon is flat-shaded from the palette, which is
  also what the `fitd` reference implementation does. See
  [docs/LIMITATIONS.md](docs/LIMITATIONS.md).

### Alone in the Dark — new assets recognised

- **Pre-rendered backdrops decode** — `.pics`, `.bob` and `.pad` files, the
  room backgrounds. `.pics` holds multiple pages and each is selectable.
  Layout confirmed by exact size arithmetic: `4 + 768 + 320×200 = 64772`
  bytes, the precise size of `CAM8003.BOB`.
- **Archive detection is now structural.** Any AITD PAK is recognised by its
  offset table rather than by filename, and model archives are told apart
  from anim/mask/floor/resource archives by probing whether a majority of
  entries carry real geometry. Non-model PAKs are listed as
  "Archive (AITD PAK)" instead of being mis-identified or ignored.

### Export

- **Models export to Wavefront OBJ + MTL.** `File > Export Selected` on a
  model archive writes one `.obj` per model plus a shared `.mtl` carrying
  the AITD palette colours. Y and Z are negated so Y-up tools (Blender,
  Maya) get the correct orientation.
- **Backdrops export to PNG**, one file per page.

### Tests

- New self-contained test target (`libm2model_synth_tests`) that synthesises
  AITD bodies and backdrop pages, pinning the PolyTexture UV fix, the
  centring fix, the no-clipping-while-orbiting guarantee, OBJ export, and
  backdrop decoding. No game data required.

---

## Previous release

### Alone in the Dark — solid 3D models

- Models render as solid, depth-sorted, palette-coloured surfaces rather
  than point clouds, with Solid (materials) / Solid (flat) / Points modes.
  The body format — vertices, bone/group tables, and polygon / line / point
  / sphere primitives — is decoded per the `fitd` and AITD_PakEdit
  references. Works for both Alone in the Dark 1 (`LISTBODY.PAK`) and 2
  (numbered `*LSTBODY.PAK`).

### Playback and audio

- **Standalone FILM audio is now correct.** It is SDX2-compressed, not the
  raw 8-bit PCM the generic FILM description (and FFmpeg's `segafilm`
  demuxer) assumes, and it was decoding as noise. Established by measuring
  lag-1 autocorrelation across candidate interpretations: 0.92 for SDX2
  versus 0.01 for PCM.
- More Need For Speed streams load — those beginning with a `CTRL` chunk.

### Images and audio formats

- Street Fighter cel-chain animations with trailing data after the last
  frame now load (`PL05_ENDING.DAT`: 26 frames; `syukyakuDEMO.dat`: 71)
  instead of erroring on the malformed final chunk.
- Road Rash's `Rash.AIFF` — actually a 3DO `RSRC` bundle of 19 AIFF sounds —
  now opens; the loader unwraps it to the first sound.

---

## Earlier releases

### Alone in the Dark — PAK archives

- PAK archives decompress and display. The compression is PKWARE DCL
  *implode*, not the LZSS originally assumed — that assumption was
  empirically disproved before the right answer was found. Decoder vendored
  from AITD-tools / Mark Adler's `blast`.

### Video and streams

- Standalone 3DO FILM support (`FILM`/`FDSC`/`STAB`) — Yu Yu Hakusho's
  `MOVIE/*.film` play (Cinepak + audio).
- Difference frames (`DFRM`) decode. Coven's `Intro.stream` went from 242
  usable frames to 6137 — it had been playing keyframes only.
- Streams beginning with a `FILL` pad block load (Need For Speed).
- MPEG frames split across chunks are reassembled correctly: `FRM]`
  continuation chunks carry no timestamp prefix, and four bytes were being
  stripped from each, corrupting the stream (Oldsmobile `72130.stream`).
- Nested `M1VC` containers are unwrapped — Pontiac's `ControlCenterUP.m1c`
  nests six deep before the real MPEG-1 640×480 payload.
- Cinepak inter-frame decoding rewritten to the canonical algorithm, fixing
  progressive block corruption; playback no longer crashes on replay.
- New file types detected: `.stream`, `.cine`, `.film`, `.movie`, `.sc`.

### Images and animation

- 1-bit packed cels decode. Packed cels use a variable-length row preamble,
  now found by validating the row chain rather than assuming a fixed offset
  (Street Fighter title screens, StarBlade's `DANGER` label).
- Multi-frame cel files are recognised as animations and play looped,
  instead of showing only their first frame.
- IMAG images that declare compression but store a full uncompressed buffer
  render completely (Yu Yu Hakusho's `STOP.IMAG`: from 99.7% black to a
  full image).
- ANIM files whose palette follows the pixel data decode (StarBlade's
  `boom.Anim`).
- Raw headerless cels are de-interleaved from M1 VRAM (LRForm) layout.

### Audio

- ADP4 (4-bit IMA/DVI ADPCM) decoding — Yu Yu Hakusho's `SOUND/*.sc`.
- CBD2 and uncompressed SNDS tracks decode alongside SDX2.

### Tools

- Extract 3DO disc images directly (ISO/BIN/IMG) via the Opera filesystem,
  handling Japanese filenames. Verified byte-identical against a reference
  extractor.
- Extract Crystal Dynamics "bigfile" archives (Gex).
- Convert images (PNG/JPG/BMP/GIF) to M2 UTF textures.
- Disassemble M2 PowerPC executables, including LZSS-compressed sections,
  with symbol names and resolved branch targets.
- "Copy Info" copies file details and full path, for bug reports.
