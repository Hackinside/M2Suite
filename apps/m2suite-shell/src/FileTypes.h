#pragma once

#include <QString>

namespace m2suite {

// M2 disc content is largely extensionless, so files are identified by
// magic bytes (first 12: container id + form type), with extension as a
// fallback. Census of real unpacked games (3DOM2VIZ, imsaM2, Polystar)
// informed this list.
enum class FileType {
    Unknown,
    UtfTexture,    // 'FORM'+'TXTR' or 'CAT '+'TXTR'  — previewable
    Cel,           // 'CCB '                            — previewable
    Anim,          // 'ANIM' — M1 cel animation         — previewable (frames)
    Imag,          // 'IMAG' — M1 image/backdrop        — previewable
    Aiff,          // 'FORM'+'AIFF'                     — previewable
    Aifc,          // 'FORM'+'AIFC'                     — previewable (PCM only)
    Wav,           // 'RIFF'+'WAVE'                     — previewable
    StandardImage, // GIF/JPEG/PNG/BMP — via Qt         — previewable
    StreamFile,    // 'SHDR' — DataStreamer .str movie/audio stream
    Instrument,    // 'FORM'+'3INS'  — recognized, no preview yet
    Font,          // 'FORM'+'FONT'  — recognized, no preview yet
    Ddf,           // 'FORM'+'DDF '  — device description, no preview yet
    Intl,          // 'FORM'+'INTL'  — locale data, no preview yet
    Pebm,          // 'PEBM'+'TAG0'  — proprietary M2 bitmap (Oldsmobile)
    M1vc,          // 'M1VC'+'TAG0'  — proprietary M1 video (Oldsmobile)
    Elf,           // 0x7F 'ELF'     — M2 executable (future disassembler)
    FilmFile,      // 'FILM'+'FDSC'  — standalone QuickTime-derived 3DO film
                   //                  (Cinepak), distinct from DataStreamer
    AitdPak,       // Alone in the Dark PAK of 3D models (LISTBODY etc.)
    AitdImage,     // AITD pre-rendered backdrop (.pics/.bob/.pad) — previewable
    AitdArchive,   // Any other AITD PAK (anims, masks, floors, resources):
                   //   listed and extractable, but not a model archive
    AitdPages,     // AITD in-game book/document pages, 'PAGES:' magic (.16X)
    AitdData,      // AITD engine tables (.ITD): objects, vars, defines
    ExportedModel, // .obj/.ply/.mtl — our own (or another tool's) export
                   //   output sitting in a game folder. Recognised so it is
                   //   not mistaken for game data.
};

FileType sniffFileType(const QString& path);
QString fileTypeLabel(FileType type);
bool fileTypeHasPreview(FileType type);

} // namespace m2suite
