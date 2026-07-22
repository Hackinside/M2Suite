# Changelog

## Unreleased

### Fixed: structural file detection was dead

Three things reported as broken shared one cause. The file sniffer read a
**12-byte** header, but the structural probes need far more — an AITD PAK
offset table is walked entry by entry, and a backdrop header is 4 bytes
followed by a 768-byte palette. Every probe failed its own length guard and
silently returned false, so **no PAK and no backdrop was ever recognised**,
however valid the file. The sniffer now reads 512 bytes and passes the real
file size separately.

- **AITD archives appear in the file browser again** — models, rooms,
  animations, masks, sound archives and scripts.
- **Backdrops open**, and a second bug surfaced once they did: pages pad to
  a **4 KiB boundary**, not to a fixed 64 KiB page. Every backdrop first
  sampled was 320×200, whose payload rounds to exactly 65536 — so the wrong
  constant fitted perfectly. AITD1's `camera00.pics` (240×200) and AITD2's
  `camera01.pics` (320×250) were rejected outright. See
  [docs/FORMATS.md](docs/FORMATS.md#pre-rendered-backdrops--pics--bob--pad).

### Sound catalogues

- **`LISTSAMP.CAT` and friends now open** — 193 sound effects, browsable and
  playable one by one, exportable as WAV. They are complete FORM/AIFF files
  concatenated on 2048-byte CD-sector boundaries. Sector alignment is what
  makes them findable: scanning for the `FORM` tag anywhere also hits the
  pattern inside sample data.

### Fixed

- **Garbled characters in the audio converter.** Codec descriptions are
  `char*` in a Qt-free library; an em dash written as UTF-8 and read back
  through `QString::fromLatin1` came out as mojibake ("the 3DO workhorse å
  what most shipping titles used"). The strings are plain ASCII now and the
  GUI decodes them as UTF-8.
- **Removed File > Load AITD Name Database.** The databases are bundled, so
  entries are named out of the box; an external or updated one is still
  picked up automatically from beside the game or beside the executable.

### Stage viewer — AITD rooms and floors

- **`ETAGE*.PAK` floors open in the 3D viewport.** A floor's rooms assemble
  into one plan you can orbit, zoom and export as `.obj` + `.mtl`. Grey is
  walkable floor, blue a link to another room, deep blue an interactive box,
  red a script trigger. Colliders show by default; the frame selector adds
  triggers, which are tall volumes that would otherwise hide the layout from
  above.
- AITD's visuals are the pre-rendered backdrops, so a room stores no mesh —
  what it stores is collision volumes, and that is what this reconstructs.
  Verified across both games: AITD1's ETAGE03 gives 14 rooms, 178 colliders
  and 43 triggers, and renders as a recognisable Derceto floor plan.
  Layout in [docs/FORMATS.md](docs/FORMATS.md#rooms-and-floors--etagepak),
  built from tigrouind's AITD-roomviewer.

### Audio encoding — write 3DO formats, not just read them

- **New: File > Convert Audio to 3DO Format.** Encodes WAV/AIFF/AIFC into
  SDX2, SQS2, CBD2, ADP4 or PCM, with mode (mono/stereo), sample rate and
  container, and a live description and bitrate for whatever is selected.
  SDX2 at 22050 Hz mono is preselected — what shipping 3DO titles actually
  used, and what the FILM/DataStreamer audio path expects.
- Bitrate is reported rather than offered as a slider: every 3DO codec is a
  fixed-ratio scheme, so the rate follows from sample rate x channels x
  bits. A bitrate control would be lying about the format.
- The delta codecs are encoded against their own decoder, searching both
  the exact and delta forms per sample. They carry per-channel history, so
  an encoder that disagrees with the decoder by one LSB drifts audibly
  rather than sounding slightly off. Round-trip RMS error: SDX2 0.13%,
  CBD2 0.10%, ADP4 4.2%.

### File recognition and filters

- **Recognition rebuilt around categories.** The filter list is now
  generated from the type table, so a new format appears under the right
  heading automatically. The old hand-maintained switch over combo-box
  indices had silently dropped every type added after it was written.
  Filters: Textures, Images, Animation, Video & streams, Audio, 3D models &
  rooms, Archives, Executables, Data & documents, Unrecognised.
- AITD archives are now told apart by role — models, rooms, animations,
  camera masks, sound archives, scripts — instead of one "Archive" bucket.
- Newly recognised: `RSRC` resource bundles, `APPSCRN` banner screens, 3DO
  M1 ARM executables, `.16X` documents, `.ITD` engine tables, `.STK` string
  tables, save games, and `.obj`/`.ply`/`.mtl`/`.gltf` export output.

### Browsing AITD archives

- **Archives expand in the file pane.** Any AITD PAK now opens in the tree
  to show its entries, each labelled with its real name and whether it holds
  geometry. Selecting an entry jumps straight to it. Expansion is lazy — a
  folder of AITD PAKs holds thousands of entries, and opening them during a
  scan would make browsing crawl.
- **Names work with no setup.** The AITD_PakEdit name databases are bundled
  (GPL-2.0-or-later, compatible with this project — see
  [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)), so entries read
  "12 — Emily" out of the box. A database beside the game, next to the
  executable, or chosen via File > Load AITD Name Database still takes
  precedence.

### CI

- **Both jobs were failing; both are fixed.** The game-data tripwire was
  matching the project's own `tests/fixtures/`, and the build job depended
  on a third-party MSVC-setup action and on `windows-latest` continuing to
  ship the Visual Studio version the preset asks for by name. The tripwire
  now exempts the reviewed fixture set, and the build job is pinned to
  `windows-2022` with no external action — the preset's Visual Studio
  generator locates the toolchain itself.
- `THIRD_PARTY_LICENSES.md` and `CONTRIBUTING.md` now state the fixture
  position honestly: the repo does carry ~1.5 MB of minimal decoder samples,
  they are listed, and synthesised inputs are the preferred pattern for
  anything new.

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
