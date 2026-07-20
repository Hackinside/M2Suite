

<img width="2272" height="1888" alt="M2Suite_Logo" src="https://github.com/user-attachments/assets/91dad2ee-d302-4e2c-b8c4-8910b9b0afcf" />


# M2Suite
Converter &amp; Visualizer for 3DO / Panasonic M2 file formats.

M2Suite — portable build
=========================

Converter & Visualizer for 3DO / Panasonic M2 file formats.

HOW TO RUN
----------
Just run  m2suite-shell.exe

Everything it needs is in this folder: the Qt runtime, the Visual C++
runtime (vcruntime140*/msvcp140*), the FFmpeg libraries, and the Qt
plugins (platforms/, styles/, imageformats/, multimedia/). No install,
no admin rights. Copy the whole folder to another Windows 10/11 x64 PC
and it will run as-is.

WHAT'S INSIDE
-------------
  m2suite-shell.exe      the application
  qt.conf                tells Qt where the plugin folders are
  *.dll                  Qt6, FFmpeg, JPEG/PNG, and MSVC runtime libraries
  platforms/             Qt Windows platform plugin (required)
  styles/                native Windows style
  imageformats/          JPEG / GIF / ICO loaders
  multimedia/            FFmpeg + Windows media backends (MPEG playback)

OPTIONAL: FFmpeg command-line tool
----------------------------------
Two features shell out to a standalone ffmpeg.exe if one is on PATH (or
at C:\ffmpeg\bin\ffmpeg.exe):
  * remuxing MPEG streams for smooth in-app playback with audio
  * exporting Cinepak videos to .mp4
Everything else — textures, cels, animations, audio, disc-image
extraction, disassembly — works without it. Grab a build from
https://www.gyan.dev/ffmpeg/builds/ if you want those two features.

KEY FEATURES
------------
  * View M2/M1 textures (UTF/M2TX), cels, ANIM animations, IMAG screens
  * Play DataStreamer video (Cinepak) and MPEG streams with audio
  * Extract 3DO disc images directly (File > Extract Disc Image):
    .iso / .bin / .img — reads the Opera filesystem, handles Japanese
    (Shift-JIS) filenames, writes to <image-name>.unpacked
  * Disassemble M2 PowerPC ELF executables (with LZSS-compressed sections)
  * Convert/export textures, audio (WAV), and video (frames or .mp4)

A run log is written to m2suite.log next to the exe — check it first if
anything misbehaves.
