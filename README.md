

<img width="2272" height="1888" alt="M2Suite_Logo" src="https://github.com/user-attachments/assets/91dad2ee-d302-4e2c-b8c4-8910b9b0afcf" />


# M2Suite
Converter &amp; Visualizer for 3DO / Panasonic M2 file formats.

M2Suite — portable build
=========================

WHAT IT IS
----------
A converter and visualizer for 3DO and Panasonic M2 game data. Point it at
an unpacked game folder (or extract a disc image with it) and it will
identify, preview, play and convert the content:

  * Textures    — M2 UTF/M2TX, including paged and palettised textures
  * Images      — 3DO cels (all bit depths, packed and coded), IMAG
                  screens, M1 VRAM (LRForm) buffers
  * Animation   — ANIM files and multi-frame cel sequences, played looped
  * Video       — DataStreamer movies (Cinepak), standalone FILM files,
                  and MPEG-1 streams, with audio
  * Audio       — AIFF/AIFC including the 3DO codecs SDX2, SQS2, CBD2 and
                  ADP4 (IMA ADPCM), plus WAV
  * Executables — M2 PowerPC ELF disassembly with symbol names, resolved
                  branch targets, and automatic decompression of
                  LZSS-compressed sections

It also extracts 3DO disc images (ISO/BIN/IMG) directly, reading the Opera
filesystem without any external tool, and converts modern images into M2
UTF textures for asset replacement or translation work.

HOW TO RUN
----------
Run  m2suite-shell.exe

Everything it needs is in this folder: the Qt runtime, the Visual C++
runtime, FFmpeg, and the Qt plugins. No install, no admin rights. Copy the
whole folder to another Windows 10/11 x64 PC and it will run as-is.

FIRST STEPS
-----------
  File > Open Folder            browse an unpacked game
  File > Extract Disc Image     unpack an .iso/.bin/.img (or a Gex bigfile)
  File > Convert Images to UTF  turn PNG/JPG into M2 textures
  View > Viewport Background    change the preview backdrop
  Help > About                  credits

Select a file to preview it. Video and animation start automatically;
use Play/Stop to control them. "Copy Info" copies the file details and
full path to the clipboard, which is handy for bug reports.

OPTIONAL: FFmpeg command-line tool
----------------------------------
Two features shell out to a standalone ffmpeg.exe if one is on PATH (or at
C:\ffmpeg\bin\ffmpeg.exe): remuxing MPEG streams for smooth in-app playback
with audio, and exporting Cinepak video to .mp4. Everything else works
without it — MPEG playback itself uses the bundled FFmpeg libraries.

TROUBLESHOOTING
---------------
A run log is written to m2suite.log next to the exe. It records what was
opened, playback backend status, and any errors or crashes — check it
first if something misbehaves.

M2Suite — change log
====================

This build
----------

Alone in the Dark 3D models — now SOLID
  * Models render as solid, depth-sorted, palette-coloured surfaces (not
    just points). The View selector offers Solid (materials) / Solid
    (flat) / Points. Body format (vertices, bones/groups, polygon / line /
    point / sphere primitives) decoded per the fitd + AITD_PakEdit
    reference. Works for both Alone in the Dark 1 (LISTBODY.PAK) and 2
    (numbered *LSTBODY.PAK).

Playback & audio
  * Standalone FILM audio is now correct — it's SDX2-compressed, not the
    raw 8-bit PCM the generic FILM spec assumes; it was decoding as noise.
    (Yu Yu Hakusho MV_01.film: verified 0.92 vs 0.01 sample correlation.)
  * More Need For Speed streams load (files starting with a CTRL chunk).

Images & audio formats
  * Street Fighter cel-chain animations that append trailing data after
    the last frame now load (PL05_ENDING.DAT: 26 frames; syukyakuDEMO.dat:
    71 frames) instead of erroring.
  * Road Rash's Rash.AIFF (a 3DO RSRC resource bundle of 19 AIFF sounds)
    now opens — the loader unwraps it to the first sound.

Previous build
--------------

Alone in the Dark 3D models
  * The PAK archives (LISTBODY / LISTBOD2) are decompressed and displayed:
    open one to browse its 3D character models as a rotating point cloud.
    The compression is PKWARE DCL "implode" (not the LZSS previously
    assumed) — decoder vendored from AITD-tools. 272/272 entries
    decompress; 226 parse as valid geometry, validated against each
    model's bounding box. (Faces and materials aren't decoded yet, so the
    view is the model's real vertex cloud, not a solid render.)

More formats
  * Standalone FILM movies now PLAY, not just parse (Yu Yu Hakusho).
  * More DataStreamer variants load: files that begin with CTRL or FILL
    instead of a stream header (several Need For Speed Movies/*.Stream).
  * Crystal Dynamics "bigfile" archives extract (Gex). Note: the entries
    are game-internal formats and aren't individually recognised.

Earlier in this release
-----------------------

Video / streams
  * Standalone 3DO FILM support ('FILM'/'FDSC'/'STAB') — Yu Yu Hakusho's
    MOVIE/*.film now play (Cinepak + 8-bit PCM audio).
  * Difference frames ('DFRM') are decoded. Coven's Intro.stream went from
    242 usable frames to 6137 — it was playing only the keyframes.
  * Streams that begin with a FILL pad block now load (Need For Speed's
    Movies/*.Stream).
  * MPEG frames split across chunks are reassembled correctly. The 'FRM]'
    continuation chunks carry no timestamp prefix; four bytes were being
    stripped from each, corrupting the stream (Oldsmobile 72130.stream).
  * Nested M1VC containers are unwrapped (Pontiac's ControlCenterUP.m1c
    nests six deep before the real MPEG-1 640x480 payload).
  * Cinepak inter-frame decoding rewritten to the canonical algorithm,
    fixing block corruption; and playback no longer crashes on replay.
  * New file types detected: .stream, .cine, .film, .movie, .sc.

Images / animation
  * 1-bit packed cels decode. Packed cels use a variable-length preamble,
    now detected by validating the row chain (Street Fighter's title
    screens, StarBlade's DANGER label).
  * Multi-frame cel files are recognised as animations and play looped
    automatically, instead of showing only their first frame.
  * IMAG images that declare compression but actually store a full
    uncompressed buffer now render completely (Yu Yu Hakusho's STOP.IMAG
    went from 99.7% black to a full image).
  * ANIM files whose palette follows the pixel data now decode
    (StarBlade's boom.Anim).
  * Raw headerless cels are de-interleaved from M1 VRAM (LRForm) layout.

Audio
  * ADP4 (4-bit IMA/DVI ADPCM) decoding — Yu Yu Hakusho's SOUND/*.sc.
  * CBD2 and uncompressed SNDS tracks decode alongside SDX2.

Tools
  * Extract 3DO disc images directly (ISO/BIN/IMG) — reads the Opera
    filesystem, handles Japanese filenames, and verified byte-identical
    against the reference tool.
  * Extract Crystal Dynamics "bigfile" archives (Gex).
  * Convert images (PNG/JPG/BMP/GIF) to M2 UTF textures.
  * Disassemble M2 PowerPC executables, including LZSS-compressed
    sections, with symbol names and resolved branch targets.
  * "Copy Info" button copies the file details and full path for reports.

Known limitations
-----------------
  * Alone in the Dark models render with flat-shaded polygons; per-face
    texture maps (PolyTexture primitives) aren't sampled yet.
  * PEBM bitmaps: the container is parsed but stores no dimensions, so the
    image can't be laid out yet.
  * CHD disc images: convert to .bin/.cue or .iso first.
  * Game-specific archives (e.g. Gex bigfile) extract, but their internal
    entry formats are proprietary and aren't individually recognised.
