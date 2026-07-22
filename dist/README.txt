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

See CHANGELOG.txt for what's new and for current known limitations.
