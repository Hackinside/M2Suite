#pragma once

#include <QString>

namespace m2suite {

// 3DO M1 and M2 disc content is largely extensionless, so files are
// identified by magic bytes (first 12: container id + form type), then by
// structural probes for the formats that carry no magic at all, and only
// then by extension. Census of real unpacked games (3DOM2VIZ, imsaM2,
// Polystar, Alone in the Dark 1 & 2) informed this list.
enum class FileType {
    Unknown,

    // --- Textures ---
    UtfTexture,    // 'FORM'+'TXTR' or 'CAT '+'TXTR'  — previewable

    // --- Images ---
    Cel,           // 'CCB '                            — previewable
    Imag,          // 'IMAG' — M1 image/backdrop        — previewable
    StandardImage, // GIF/JPEG/PNG/BMP — via Qt         — previewable
    Pebm,          // 'PEBM'+'TAG0' — proprietary M2 bitmap (Oldsmobile)
    AitdImage,     // AITD backdrop (.pics/.bob/.pad)   — previewable
    Banner,        // 'APPSCRN' — 3DO disc banner screen

    // --- Animation ---
    Anim,          // 'ANIM' — M1 cel animation         — previewable (frames)

    // --- Video / streams ---
    StreamFile,    // 'SHDR' — DataStreamer movie/audio stream
    FilmFile,      // 'FILM'+'FDSC' — standalone 3DO film (Cinepak)
    M1vc,          // 'M1VC'+'TAG0' — proprietary M1 video (Oldsmobile)

    // --- Audio ---
    Aiff,          // 'FORM'+'AIFF'                     — previewable
    Aifc,          // 'FORM'+'AIFC'                     — previewable
    Wav,           // 'RIFF'+'WAVE'                     — previewable
    RsrcBundle,    // 'RSRC' — resource bundle, commonly of AIFFs
    SoundCatalogue,// many FORM/AIFF sounds concatenated (AITD LISTSAMP.CAT)
    Instrument,    // 'FORM'+'3INS' — DSP instrument

    // --- 3D ---
    AitdPak,       // AITD archive of 3D models (LISTBODY / *LSTBODY)
    AitdRooms,     // AITD floor/room geometry (ETAGE*.PAK)
    ExportedModel, // .obj/.ply/.mtl/.gltf — export output, not game data

    // --- Archives ---
    AitdArchive,   // Any other AITD PAK: listed and extractable
    AitdAnimPak,   // AITD keyframe animations (LISTANIM / *LSTANIM / ANIM*)
    AitdMaskPak,   // AITD camera clipping masks (MASK*.PAK)
    AitdSoundPak,  // AITD sound-sample archive (LISTSAMP / chsamp)

    // --- Executables ---
    Elf,           // 0x7F 'ELF'  — M2 PowerPC executable
    ArmExecutable, // 3DO M1 ARM binary (no magic; branch-led entry point)

    // --- Data ---
    Font,          // 'FORM'+'FONT'
    Ddf,           // 'FORM'+'DDF ' — device description
    Intl,          // 'FORM'+'INTL' — locale data
    AitdPages,     // AITD documents, 'PAGES:' magic (.16X)
    AitdData,      // AITD engine tables (.ITD): objects, vars, defines
    AitdScript,    // AITD scripts/tables (LSTLIFE / LSTTRAK / LSTMAT / HYB)
    AitdSave,      // AITD save game
    TextFile,      // plain text / string table
};

// Broad grouping used by the type filter. Adding a FileType only requires
// slotting it into fileTypeCategory(); the filter UI is generated from
// this list, so the two can no longer drift apart — which they did while
// the filter was a hand-maintained switch over combo-box indices.
enum class FileCategory {
    All,
    Textures,
    Images,
    Animation,
    Video,
    Audio,
    Models3D,
    Archives,
    Executables,
    Data,
    Unrecognised,
};

FileType sniffFileType(const QString& path);
QString fileTypeLabel(FileType type);
bool fileTypeHasPreview(FileType type);

FileCategory fileTypeCategory(FileType type);
// Display name for a filter entry, e.g. "3D models".
QString fileCategoryLabel(FileCategory category);
// The categories offered in the filter combo, in display order.
const QList<FileCategory>& fileCategoryOrder();

// True when the type is an AITD PAK archive of any kind — the container
// layout is identical, only the payload differs.
bool isAitdArchiveType(FileType type);

} // namespace m2suite
