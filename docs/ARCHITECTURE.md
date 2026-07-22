# Architecture

M2Suite is a set of standalone format libraries plus one Qt application that
drives them. The split is deliberate: **every format decision lives in a
library with no Qt dependency**, so it can be tested headlessly, reused from
a command-line tool, and reasoned about without a GUI in the way.

```
apps/
  m2suite-shell/       Qt 6 GUI — the only Qt-dependent code
libs/
  libm2core/           byte readers/writers, IFF chunk containers
  libm2texture/        M2 UTF / M2TX textures
  libm2cel/            3DO cels, ANIM, IMAG
  libm2audio/          AIFF/AIFC, WAV, 3DO codecs (SDX2/SQS2/CBD2/ADP4)
  libm2stream/         DataStreamer, standalone FILM, Cinepak, MPEG extraction
  libm2disasm/         PowerPC ELF disassembly + pseudocode lifting
  libm2disc/           Opera filesystem, disc-image extraction
  libm2model/          Alone in the Dark: PAK, bodies, renderer, backdrops
tests/                 One test target per library
cmake/                 Toolchain triplets, build-number generation
```

Libraries depend downward only. `libm2core` is the base; nothing depends on
`m2suite-shell`.

## The libraries

### `libm2core`

`ByteReader` / `ByteWriter` with bounds checking, and `IffForm` /
`IffFormWriter` for chunk containers. `IffForm` handles both M2's 4-byte
chunk alignment and IFF-85's 2-byte alignment — see
[FORMATS.md](FORMATS.md#iff-style-containers).

Errors are exceptions (`FormatError`, `NotImplementedError`). A library
**never guesses** when it does not understand something: an unsupported
texel format throws rather than producing a plausible-looking wrong image.
That rule is why M2Suite's output can be trusted.

### `libm2texture`

`Texture::load` parses the UTF header, PIP palette, DCI and LOD data;
`decodeLodToRgba` renders one LOD. Currently limited to PIP-indexed
uncompressed 8-bit-per-index textures.

### `libm2cel`

`Cel` (all bit depths, packed and unpacked), `Anim` (ANIM chunks and bare
cel chains), `Imag` (M1 full-screen images, linear and LRForm).

### `libm2audio`

`Aiff` (which also unwraps `RSRC` bundles), `Wav`, and the codec decoders in
`Sdx2.cpp` — SDX2, SQS2, CBD2 and ADP4 despite the file name.

### `libm2stream`

`Stream::load` auto-detects a DataStreamer container versus a standalone
`FILM` and normalises both into the same fields, so callers do not branch.
`CinepakDecoder` is a standalone frame decoder.

### `libm2disasm`

`Elf` loads M2 PowerPC executables, decompressing LZSS sections
transparently; `disassembleAll` produces a listing with resolved branch
targets and symbol names, and `Pseudocode` lifts it to ANSI-C-like output.

### `libm2disc`

Opera filesystem reader over ISO/BIN/IMG images. Verified byte-identical
against a reference extractor.

### `libm2model`

Alone in the Dark support, split by concern:

| File | Responsibility |
|---|---|
| `AitdPak.cpp` | PAK archive: offset table, entry headers, decompression |
| `third_party/pak_explode.cpp` | Mark Adler's PKWARE DCL *implode* decoder (vendored) |
| `AitdBody.cpp` | **Parser only** — body blob to vertices + primitives |
| `AitdRender.cpp` | Software renderer and Wavefront OBJ/MTL exporter |
| `AitdPalette.cpp` | The fixed 256-colour AITD palette |
| `AitdImage.cpp` | `.pics`/`.bob`/`.pad` pre-rendered backdrops |

Keeping the parser free of rendering concerns matters: every "broken model"
report so far turned out to be a *rendering* bug, and a clean split is what
made that quick to establish.

## The shell

`apps/m2suite-shell` is a three-pane Qt Widgets application: a filterable
file tree on the left, a view stack in the centre, an info pane on the
right.

| File | Responsibility |
|---|---|
| `FileTypes.cpp` | Type sniffing (magic bytes first, extension as fallback, structural probes where there is no magic) |
| `MainWindow.cpp` | Browsing, per-type display handlers, playback, export |
| `ImageViewport.cpp` | Zoomable still/frame viewport |
| `ModelViewport.cpp` | Interactive 3D viewport |
| `WaveformView.cpp` | Audio waveform |

The view stack swaps between a placeholder, the image viewport, the
waveform, a `QVideoWidget`, a text view, and the model viewport.

### Why the 3D viewport is software-rendered, not Qt Quick 3D

Qt Quick 3D was considered and deliberately not used. It would pull a QML
runtime and the Quick3D module into a portable package that is meant to be
unzipped and run, it would add a GPU requirement to an application that
otherwise has none, and the models in question are a few hundred flat-shaded
polygons — well inside what a scanline rasteriser handles at interactive
rates on the CPU.

`ModelViewport` is therefore a plain `QWidget` that calls
`m2model::renderAitdBody` into an RGBA buffer and blits it. Controls are the
conventional ones: left-drag orbits, right/middle-drag pans, the wheel
zooms, double-click resets, and an idle spin runs until you touch it. The
camera is a plain struct (`AitdCamera`) owned by the widget and passed to
the renderer, which keeps the renderer stateless and testable.

The trade-off is real and worth naming: there is no per-pixel lighting, no
anti-aliasing, and no path to complex scenes. If M2Suite ever needs to
display large textured environments, this decision should be revisited
rather than extended.

## Testing

One test target per library, registered with ctest. Tests are
**self-contained** — they synthesise their inputs or use small checked-in
fixtures, so the suite runs without any game data. This is not a purity
argument: game data cannot be redistributed, so a test that needs it is a
test that only one person can run.

`tests/libm2model_tests/test_aitd_synth.cpp` is the model of what to aim
for. It builds AITD bodies byte by byte and pins specific past bugs:
PolyTexture UVs desynchronising the primitive stream, the renderer framing
on the declared bounding box instead of the geometry, unreferenced vertices
dragging the centre, and models clipping while the camera orbits.

`tests/libm2model_tests/test_aitd_pak.cpp` is the exception — a manual
diagnostic tool that takes a real PAK path. It is built but not registered
with ctest.
