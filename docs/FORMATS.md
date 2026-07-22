# Format findings

Everything M2Suite knows about 3DO and Panasonic M2 data, written down so
the knowledge survives independently of the code. Each entry says what the
layout is, how it was established, and — where it matters — which published
description is **wrong**.

Conventions: `u8/u16/u32` unsigned, `s16` signed, `[n]` array. 3DO M1 and M2
chunk containers are **big-endian**; Alone in the Dark data is
**little-endian** (it is a DOS port). Byte offsets are from the start of the
structure being described.

**Contents**

- [Reading this document](#reading-this-document)
- [IFF-style containers](#iff-style-containers)
- [Textures — UTF / M2TX](#textures--utf--m2tx)
- [Cels — CCB / PDAT / PLUT](#cels--ccb--pdat--plut)
- [ANIM and cel chains](#anim-and-cel-chains)
- [IMAG screens and LRForm](#imag-screens-and-lrform)
- [RSRC resource bundles](#rsrc-resource-bundles)
- [Audio codecs](#audio-codecs)
- [DataStreamer movies](#datastreamer-movies)
- [Standalone FILM](#standalone-film)
- [Cinepak](#cinepak)
- [MPEG-1 in 3DO containers](#mpeg-1-in-3do-containers)
- [Opera filesystem](#opera-filesystem)
- [M2 PowerPC executables](#m2-powerpc-executables)
- [Alone in the Dark](#alone-in-the-dark)
- [Formats that resisted](#formats-that-resisted)

---

## Reading this document

Findings are graded, because not all of them are equally certain:

- **Verified** — decoded output was checked against something independent
  (a reference implementation, a known-good render, a statistical test, or
  an exact file-size match).
- **Consistent** — the layout explains every file we have, but nothing
  external corroborates it.
- **Assumed** — a working guess that has not failed yet.

---

## IFF-style containers

3DO and M2 both use `FORM`-style chunk containers, but with different
padding rules, and the difference is not cosmetic.

```
u32 tag         four-character chunk id
u32 size        chunk size INCLUDING these 8 header bytes
u8  payload[size - 8]
u8  pad[]       to the alignment boundary
```

**Finding (verified):** M2 chunks align to **4 bytes**; classic IFF-85
chunks align to **2**. A parser that assumes one alignment silently
desynchronises on the other after the first odd-sized chunk. `libm2core`'s
`IffForm` accepts both and picks per container.

Container ids seen in the wild: `FORM`, `CAT `, `LIST`. A `CAT ` behaves as
a concatenation of `FORM`s and must be walked, not treated as a leaf.

---

## Textures — UTF / M2TX

M2's native texture format. `FORM`+`TXTR`, or `CAT `+`TXTR` for a bundle of
several textures in one file.

Sub-chunks:

| Chunk | Contents |
|---|---|
| `M2TX` | Header: dimensions, texel format, LOD count, flags |
| `M2PI` | PIP — the colour lookup table (palette) |
| `M2CI` | DCI — colour/alpha component description |
| `M2TD` | The texel data itself, LOD by LOD |
| `M2LR` | LOD run lengths for compressed texel data |

**Finding (consistent):** LOD 0 is the largest mip level and appears first.
Texel data for later LODs follows contiguously; the per-LOD sizes come from
the header's dimensions, halved and floored per level, not from a stored
table.

**Currently decoded:** PIP-indexed, uncompressed, 8-bits-per-index. Other
texel formats (RLE-compressed runs, direct/non-palette formats) are
*detected and rejected* rather than guessed at — see
[LIMITATIONS.md](LIMITATIONS.md).

Ground truth for this format is the SDK Mercury texture library C source;
see [REFERENCES.md](REFERENCES.md).

---

## Cels — CCB / PDAT / PLUT

The 3DO M1 sprite/bitmap primitive. A cel is a chunk chain:

| Chunk | Contents |
|---|---|
| `CCB ` | Cel control block: flags, dimensions, bit depth, pixel-data offsets |
| `PDAT` | The pixel data |
| `PLUT` | Palette lookup table (16-bit `0RRRRRGGGGGBBBBB` entries) |
| `XTRA` | Optional extra description |

Bit depths 1, 2, 4, 6, 8 and 16 all occur. Depths below 8 are **coded**
(palette-indexed via the PLUT); 16-bit is direct colour.

### Packed cels

Packed (RLE) cels store rows as a chain: each row begins with a length
word, then a run of control bytes selecting literal / repeat / transparent
spans.

**Finding (verified):** 1-bit packed cels use a **variable-length row
preamble** — the offset from the row start to the first control byte is not
fixed. Guessing it produces plausible-looking garbage. The reliable test is
to *validate the row chain*: walk all rows using a candidate preamble length
and accept it only if every row lands exactly on the next row's start and
the last row ends at the end of the data. Verified against Street Fighter's
title screens and StarBlade's `DANGER` label.

### Headerless cels

Some files are raw pixel data with no `CCB `. These are M1 VRAM captures
and are stored in **LRForm** layout: the framebuffer is split into two
interleaved halves (even and odd scanlines in separate regions), so the
image must be de-interleaved before it looks like anything.

---

## ANIM and cel chains

Two different things both behave as animations:

1. **`ANIM` files** — an explicit animation chunk wrapping a sequence of
   cels, with a frame count and frame rate in the header.
2. **Cel chains** — a file that is simply several `CCB ` chunks
   concatenated. There is no header saying "this is an animation"; the only
   signal is that a second `CCB ` follows the first. M2Suite detects this by
   walking the flat chunk chain and stopping as soon as it sees a second
   `CCB `, which keeps the check cheap on large files.

**Finding (verified):** A `PLUT` may appear **after** the pixel data it
belongs to, not before. A parser that only applies a palette forward
produces a black or miscoloured frame. M2Suite back-fills a trailing `PLUT`
onto the preceding frame. Verified against StarBlade's `boom.Anim`.

**Finding (verified):** Real files append **trailing data after the last
frame** — padding, or another asset entirely. Treating a bad chunk size as
fatal throws away a perfectly good animation. The rule that works: if at
least one frame has already been decoded, stop cleanly at the bad chunk;
only error if nothing was decoded at all. Verified against Street Fighter's
`PL05_ENDING.DAT` (26 frames recovered) and `syukyakuDEMO.dat` (71 frames),
both of which previously failed outright with `anim chunk '...' has bad
size`.

**Finding (consistent):** Some cel chains carry no header at all, so a
synthetic one is derived from the first cel's CCB.

---

## IMAG screens and LRForm

`IMAG` is a full-screen M1 image, typically 16-bit `0555`.

**Finding (verified):** Some IMAG files **declare a compression mode in the
header but store a complete uncompressed buffer anyway**. Trusting the
header yields a mostly-black image. The fix is to check whether the payload
size already equals `width × height × 2` and, if so, take it verbatim.
Verified against Yu Yu Hakusho's `STOP.IMAG`, which went from 99.7% black
pixels to a full image.

Both linear and LRForm (interleaved half-framebuffer) layouts occur, and
the same de-interleave as for headerless cels applies.

---

## RSRC resource bundles

A 3DO resource archive that wraps several assets in one file.

```
'RSRC'  header
'RTBL'  resource table
        u32 count
        records[count]   32 bytes each: type tag, id, offset, size, name
payload
```

**Finding (verified):** Files with an `.AIFF` extension are sometimes
actually `RSRC` bundles. Road Rash's `Rash.AIFF` is an RSRC containing 19
separate AIFF sounds, and a strict AIFF loader rejects it with
`expected 'FORM', got 'RSRC'`. M2Suite detects the `RSRC` magic at the top
of `Aiff::load` and unwraps to the first `AIFF` resource.

---

## Audio codecs

3DO AIFF/AIFC files use several proprietary codecs identified by the
compression tag in the `COMM` chunk.

| Tag | Codec | Ratio | Notes |
|---|---|---|---|
| `NONE` / (absent) | Linear PCM 8 or 16-bit | 1:1 | |
| `SDX2` | Square-Delta 2:1 | 2:1 | Squared-delta with sign; per-channel state |
| `SQS2` | SDX2 variant | 2:1 | |
| `CBD2` | Callisto Block Delta | 2:1 | |
| `ADP4` | IMA/DVI ADPCM, 4-bit | 4:1 | Canonical 89-entry step table |
| `sowt` | Byte-swapped PCM | 1:1 | Not yet supported |

**Finding (verified):** AIFF sample rates are stored as an **80-bit IEEE 754
extended float**, not an integer. It must be decoded properly; truncating
the mantissa gives rates that are close but wrong, and the drift is audible
over a long file.

**Finding (verified):** `ADP4` is standard IMA ADPCM with the canonical
89-entry step-size table and the standard 16-entry index-adjust table — not
a 3DO-specific variant. Verified against Yu Yu Hakusho's `SOUND/*.sc`.

### Identifying an unknown codec statistically

**Technique (verified, and reusable):** when you have a byte stream and
several candidate interpretations, decode it under each and measure the
**lag-1 autocorrelation** of the resulting samples. Real audio is strongly
correlated sample-to-sample; a wrong interpretation is close to noise.

This settled the FILM audio question outright:

| Interpretation | Lag-1 autocorrelation |
|---|---|
| Signed 8-bit PCM (what FFmpeg's `segafilm` assumes) | 0.013 |
| Unsigned 8-bit PCM | 0.014 |
| **SDX2** | **0.923** |

No listening test needed — 0.92 versus 0.01 is not ambiguous.

---

## DataStreamer movies

3DO's streaming container, used for in-game movies. A flat sequence of
chunks, each:

```
u32 tag
u32 size        including this 8-byte header
u32 time        stream-tick timestamp
u32 channel
payload
```

| Tag | Contents |
|---|---|
| `SHDR` | Stream header: tick rate, buffer sizing, channel map |
| `FILM` | Video subchunk (see below) |
| `SNDS` | Audio subchunk |
| `CTRL` | Control/marker |
| `FILL` | Padding |

`FILM` subchunks carry a further sub-tag: `FHDR` (film header), `FRME`
(keyframe), `DFRM` (difference frame).

**Finding (verified):** Files exist that begin with `CTRL` or `FILL` rather
than `SHDR`. Requiring a stream header at offset 0 rejects them. M2Suite
scans forward, counting payload chunks, and accepts the file if the chunk
chain is coherent. Verified against several Need For Speed `Movies/*.Stream`.

**Finding (verified):** `DFRM` difference frames must be decoded, not
skipped. A decoder that only handles `FRME` plays keyframes only and looks
like a slideshow — Coven's `Intro.stream` went from **242 usable frames to
6137** once `DFRM` was handled.

**Finding (verified) — A/V sync:** frame timestamps are in stream ticks, and
the tick rate in `SHDR` is not always usable. Deriving the effective rate
from the **audio track's true decoded duration** and then showing the newest
frame whose timestamp is due keeps long clips in sync, and tolerates
variable frame spacing. Assuming a constant frame rate drifts.

---

## Standalone FILM

The same `FILM` payload without the DataStreamer wrapper — a QuickTime
derivative:

```
'FILM'  size  ...
  'FDSC'   film description: codec fourcc, width, height
  'STAB'   sample table: per-sample offset, size, timestamp, flags
  'FRME'   the samples themselves
```

**Finding (verified) — the audio is SDX2, not PCM.** The generic FILM/Sega
FILM description (and FFmpeg's `segafilm` demuxer) treats 8-bit FILM audio
as raw signed PCM. On 3DO discs it is **SDX2-compressed at 22050 Hz, mono,
decoding to 16-bit**. Decoding it as PCM produces loud noise. Established by
the autocorrelation test above against Yu Yu Hakusho's `MOVIE/MV_01.film`.

**Finding (consistent):** `DFRM` appears here too, alongside `FRME`, and
both must be accepted as sample types.

---

## Cinepak

The video codec inside almost all 3DO `FILM` payloads (`cvid`).

**Finding (verified):** inter-frame (delta) decoding must follow the
canonical algorithm exactly — in particular, the codebook update flags and
the "skip" run semantics. An approximation produces progressive block
corruption that only becomes obvious dozens of frames in. M2Suite's decoder
was rewritten against the reference algorithm after exactly that failure.

---

## MPEG-1 in 3DO containers

Some discs carry MPEG-1 elementary streams inside 3DO containers.

**Finding (verified):** when an MPEG frame spans several chunks, the
continuation chunks use the tag `FRM]` and — unlike the first chunk —
**carry no timestamp prefix**. Stripping four bytes from every chunk
uniformly corrupts the stream. Verified against Oldsmobile's
`72130.stream`.

**Finding (verified):** `M1VC` containers nest. Pontiac's
`ControlCenterUP.m1c` wraps its real MPEG-1 640×480 payload **six levels
deep**. Unwrapping needs to recurse with a depth guard rather than peel one
layer.

---

## Opera filesystem

The 3DO disc filesystem, present on M1 and M2 discs.

- 2048-byte logical blocks; the volume header is at block 0 and carries the
  magic byte `0x01` plus the `ZZZZZ` synchronisation pattern.
- Directories are block-chained; each entry has a type tag, block count,
  and a fixed-length name field.
- Filenames may be Shift-JIS on Japanese discs and must not be forced
  through a Latin-1 conversion.

**Finding (verified):** M2Suite's extractor was checked **byte-identical**
against an established reference extractor across full discs, which is what
makes disc extraction trustworthy enough to build everything else on.

`.chd` images are *not* supported — convert to `.bin`/`.cue` or `.iso`
first.

---

## M2 PowerPC executables

M2 runs PowerPC 602. Executables are ELF.

**Finding (consistent):** sections are frequently **LZSS-compressed** and
must be inflated before disassembly, or the listing is noise. M2Suite
detects and decompresses these automatically.

The disassembler resolves branch targets and applies symbol names from the
ELF symbol table, and a second pass lifts the listing into ANSI-C-like
pseudocode.

---

## Alone in the Dark

The 3DO ports of *Alone in the Dark* 1 and 2 keep the DOS engine's data
formats, so everything here is **little-endian**. This is the most complete
set of findings in the project.

### PAK archives

```
u32 offsets[N]        offsets[0] == 0; offsets[1] doubles as the table size,
                      so N = offsets[1] / 4  and  entryCount = N - 1

per entry, at offsets[i]:
  u32 skip            size of the extra header, including this field
  u8  extra[skip - 4]
  u32 compressedSize
  u32 uncompressedSize
  u8  compressionType
  u8  compressionFlags
  u16 padding
  u8  payload[]       at entryOffset + 16 + extraLen + padding
```

**Finding (verified) — the compression is PKWARE DCL "implode", not LZSS.**
This cost real time. `compressionType == 1` was initially assumed to be the
LZSS used elsewhere in the engine, and that assumption was *empirically
disproved* before the right answer was found: the first control byte `0x0F`
decodes as "four literals, then a back-reference to offset 789", which is
impossible when only four bytes of output exist. The actual codec is PKWARE
Data Compression Library *implode*, decoded with Mark Adler's public-domain
`blast`. In the compression flags, bit 2 (`0x04`) means a literal Huffman
tree is present and bit 1 (`0x02`) selects the 8 KiB dictionary.

`compressionType == 0` is stored; `4` is a deflate-like variant.

### Body (3D model) format

Per the `fitd` reference implementation (`hqr.cpp`, `createBodyFromPtr`):

```
u16 flags
s16 bbox[6]           ZVX1, ZVX2, ZVY1, ZVY2, ZVZ1, ZVZ2
u16 scratchSize
u8  scratch[scratchSize]
u16 numVertices
s16 vertices[numVertices][3]
                      -- if flags & INFO_ANIM (2):
u16 numGroups
u16 groupOrder[numGroups]
u8  groupRecords[numGroups][recSize]
                      recSize = 0x18 if flags & INFO_OPTIMISE (8), else 0x10
u16 numPrimitives
    primitives[numPrimitives]
```

Flags: `INFO_ANIM = 2`, `INFO_TORTUE = 4`, `INFO_OPTIMISE = 8`.

**Finding (verified) — the field at offset 14 is `scratchBufferSize`, not
the vertex count.** An early heuristic that searched for a vertex count near
the header found a value that "fit" and produced geometry that looked
*almost* right. It is actually the size of a variable-length scratch buffer
that sits *before* the real count, so every model was being read at the
wrong offset. There is no shortcut here: the layout must be walked exactly.

### Bone groups — vertices are NOT in model space

**Finding (verified) — the single most consequential AITD finding so far.**

A body with `INFO_ANIM` stores its vertices in **group-local space**. Each
group (bone) owns a run of vertices, and every one of them is an *offset
from the position of that group's base vertex* — the vertex it hangs off in
its parent. A parser that stops after reading the vertex array gets a mesh
whose limbs are all bunched around the origin.

To resolve it, walk the groups **in stored order, in place**:

```
for each group g, in the order they appear in the file:
    base = vertices[g.baseVertex]          // already in model space
    for k in 0 .. g.vertexCount-1:
        vertices[g.start + k] += base
```

Two details are load-bearing:

- **Stored order matters.** Groups are laid out parents-first, so by the
  time a child is reached its base vertex has already been moved into model
  space and the offsets cascade correctly down the chain.
- **It must be in place.** Resolving against a pristine copy of the vertex
  array breaks every group below the first level, because children need the
  parent's *resolved* position, not its original one.

`start` and `baseVertex` are stored as byte offsets — divide by 6.

Group record layout, following the group-order table (one `u16` per group):

| Field | Size | Notes |
|---|---|---|
| `start` | u16 | first vertex, byte offset (÷6) |
| `numVertices` | u16 | how many vertices the group owns |
| `baseVertices` | u16 | base vertex, byte offset (÷6) |
| `orgGroup` | s8 | parent group index, −1 at the root |
| `numGroup` | s8 | |
| `state.type` | s16 | 0 = rotate, 1 = translate, 2 = zoom |
| `state.delta[3]` | s16 × 3 | animation deltas, zero in the bind pose |
| `state.rotateDelta[3]` + pad | s16 × 4 | **AITD2 only** (`INFO_OPTIMISE`) |

So the record is **0x10 bytes on AITD1** and **0x18 on AITD2**. Reading the
wrong stride swallows the primitive count and the whole primitive list
decodes as garbage.

**How it was caught:** `LISTBOD2.PAK` entry 12 (AITD1) is Emily Hartwood.
It rendered as a jumble of overlapping shards. Comparing against a
reference render of the same body made it obvious the geometry was present
but misplaced, not corrupt — which pointed at a transform rather than a
parse error. Pinned by regression tests
(`test_aitd_synth.cpp`, `testGroupHierarchyResolves`).

### Primitives

```
u8 type
```

| Type | Name | Payload after `type` |
|---|---|---|
| 0 | Line | `u8 subType, u8 color, u8 even, u16 points[2]` |
| 1 | Poly | `u8 n, u8 subType, u8 color, u16 points[n]` |
| 2 | Point | `u8 subType, u8 color, u8 even, u16 points[1]` |
| 3 | Sphere | `u8 subType, u8 color, u8 even, u16 size, u16 points[1]` |
| 4 | Disk | (not observed in 3DO data) |
| 5 | Cylinder | (not observed in 3DO data) |
| 6 | BigPoint | as Point |
| 7 | Zixel | as Point |
| 8 | PolyTexture8 | as Poly |
| 9 | PolyTexture9 | as Poly, **then `u8 uv[n][2]`** |
| 10 | PolyTexture10 | as Poly, **then `u8 uv[n][2]`** |

**Finding (verified) — vertex indices are stored as byte offsets.** Divide
each `u16` by 6 (three `s16` per vertex) to get the vertex index.

**Finding (verified) — PolyTexture 9 and 10 append a `(u,v)` byte pair per
point; type 8 does not.** These UVs sit inline in the primitive stream, so a
parser that skips them **desynchronises every following primitive**. The
symptom is distinctive: models grow long thin spikes shooting off into
space, because garbage indices reference distant vertices. This is pinned by
a regression test (`tests/libm2model_tests/test_aitd_synth.cpp`).

**Finding (verified) — there is no textured geometry in the 3DO builds.** A
sweep of *every* PAK in both games found **zero** primitives of type 8, 9 or
10 in any real model archive:

| Archive | Game | Bodies | Primitive types present |
|---|---|---|---|
| `LISTBODY.PAK` | AITD 1 | 272 | 0, 1, 2, 3, 6, 7 |
| `LISTBOD2.PAK` | AITD 1 | 272 | 0, 1, 2, 3, 6, 7 |
| `4LSTBODY.PAK` | AITD 2 | 551 / 553 | 0, 1, 2, 3, 6, 7 |

Every polygon is flat-shaded from the palette. So "sample the PolyTexture
primitives" has no data to act on for these two ports — the UV parsing is
implemented and tested so that a build which *does* use them (the PC
releases, or AITD 3) decodes correctly, but on 3DO there is nothing to
sample. The reference implementation `fitd` reaches the same conclusion from
the other direction: it renders PolyTexture primitives with
`processPrim_Poly`, i.e. flat, and never samples a texture.

### Which archives hold what

Determined by sweeping every `.PAK` and counting entries that parse as real
geometry (≥4 vertices and ≥2 primitives):

| Archive | Contents |
|---|---|
| `LISTBODY.PAK`, `LISTBOD2.PAK`, `*LSTBODY.PAK` | **3D models** |
| `LISTANIM.PAK`, `LISTANI2.PAK`, `5LSTANIM.PAK` | Model animations |
| `ETAGE00..15.PAK` | Floors / rooms |
| `MASK00..15.PAK` | Camera masks (2D clipping polygons) |
| `ANIM00..15.PAK` | Room animations |
| `ITD_RESS.PAK`, `JAP_RESS.PAK` | Engine resources |
| `LISTSAMP.PAK`, `chsamp.pak` | Sound samples |
| `0LSTMAT / 1LSTLIFE / 2LSTTRAK / 3LSTHYB` | Scripts and tables |

**Trap:** almost any blob can be coerced through the body parser and yield
one or two "valid" bodies. MASK PAKs in particular produce a handful of
false positives. Identifying a model archive therefore requires a
**majority** of sampled entries to carry real geometry, not merely one.

### Pre-rendered backdrops — `.pics` / `.bob` / `.pad`

The room backgrounds. Three container extensions, one payload layout:

```
u16 width          320 on every file seen
u16 height         200
u8  palette[768]   256 RGB triples, 8 bits per channel
u8  pixels[w*h]    one palette index per pixel
```

- `.bob` — a single un-padded page
- `.pad` — a single page padded to 64 KiB
- `.pics` — one or more pages, **each** padded to 64 KiB

**Finding (verified) — by exact arithmetic.** `4 + 768 + 320×200 = 64772`,
which is the exact byte size of `CAM8003.BOB`. That is not a coincidence,
and it confirms the palette size and pixel depth in one step. Decoded output
was then visually checked: recognisable AITD 2 rooms (the tree and mansion
exterior, the cellar stairs, the attic bedroom).

### Palette

The 256-colour AITD palette is a fixed table, transcribed from
AITD_PakEdit's `AloneFile::palette` and vendored as
`libs/libm2model/src/AitdPalette.cpp`. Primitive `color` fields index it
directly.

### Coordinate system

**Finding (verified):** AITD is **Y-down** — negative Y is up. A standing
character has feet near `y = 0` and head near `y = -1700`. Because screen Y
also increases downward, model Y maps straight to screen Y and models render
upright with no flip. Exporters targeting Y-up tools (OBJ) must negate Y
**and** Z to preserve handedness; M2Suite's OBJ exporter does.

### Entry name databases

AITD_PakEdit ships hand-curated JSON databases (`AITD1_CD_PAK_DB.json`,
`AITD1_floppy_PAK_DB.json`, …) that name the contents of each archive
entry. They are the community's accumulated identification work and turn
"Model 12" into "Emily Hartwood".

```json
{ "all_PAKs": {
    "CAMERA02.PAK": {
      "25": { "default_compr": 1, "info": "1st Floor Library (Secret Room)", "type": 2 }
    } } }
```

Keys are archive filenames, then entry index as a string. `info` is `"?"`
when the entry has not been identified. The `type` field is a content
class:

| `type` | Meaning |
|---|---|
| 0 | Unclassified / other |
| 1 | Text (in-game messages, readable documents) |
| 2 | Background image |
| 3 | Floor / room layout |
| 4 | Camera data |
| 6 | Full-screen image sequence |
| 7 | 3D model (body) |

These databases doubled as an **independent check on the renderer**. After
the bone-group fix, a contact sheet of `LISTBOD2.PAK` was rendered blind and
then compared against the database names: entry 12 "Emily", entry 5 "4 Pane
Window", entry 0 "Wooden Chest (Closed), Loft", entry 20 "Indian Cover" —
every one matched what had been drawn. Cheap, and much stronger evidence
than eyeballing a single model.

M2Suite reads any `*PAK_DB.json` sitting beside the archive or one level up
and uses it to label entries. It **does not bundle one** — the databases
belong to their authors, and are game-specific.

### Rendering traps

Two bugs here produced the "broken models" symptom, and neither was a
parsing problem:

**The declared bounding box is not the geometry.** It doubles as a collision
volume and can be an order of magnitude larger than the mesh. Framing the
camera on it renders the model as a speck in the corner. Fit to the actual
vertex extents.

**Unreferenced vertices drag the centre.** The vertex array contains
vertices no primitive references — bone roots and animation helpers,
typically sitting at the origin. Including them in the extents pushes models
low in the frame and clips them off the bottom edge. Fit only to vertices
that a primitive actually uses.

The combination that works: compute the centre from the extents of
*referenced* vertices, then scale by the **bounding sphere radius** rather
than per-axis extents. The sphere radius is rotation-invariant, so the model
keeps a constant size and never clips as the camera orbits. Both properties
are pinned by regression tests.

**Painter's algorithm is not enough.** Depth-sorting whole faces renders
interpenetrating geometry incorrectly — limbs crossing a torso visibly tear.
A per-pixel depth buffer, with depth interpolated across each scanline span,
fixes it. Sorting is kept as a cheap first pass.

**AITD has no lighting model — do not add one.** The artists baked shading
into their *choice of palette index per face*, which is why adjacent faces
already read as lit. Applying a light term on top double-shades the model;
combined with a two-sided winding test it dimmed most of the mesh, and
Emily Hartwood came out near-black next to a reference render of the same
body. The faithful mode renders the palette colour unmodified. A neutral
grey mode keeps the shading, because there the point is to read silhouette
and form rather than to reproduce the original.

---

## Formats that resisted

Documented so nobody repeats the search:

- **Gex "bigfile"** (Crystal Dynamics) — the archive structure is understood
  and entries extract cleanly, but the *entries themselves* are
  game-internal formats with no magic and no public description. QuickBMS
  ships no Gex script; its repository is source-only for this title.
- **PEBM** (Oldsmobile) — the container parses, but it stores **no width or
  height anywhere**, so the pixel buffer cannot be laid out. Solving this
  needs either a companion file that carries the dimensions or a sample
  whose dimensions are known from elsewhere.

---

*Corrections and additions are welcome — see
[CONTRIBUTING.md](../CONTRIBUTING.md). A finding backed by a reproducible
observation is worth more than a plausible theory, and a finding that
disproves something on this page is worth most of all.*
