#include "FileTypes.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>

#include "m2audio/Aiff.h"
#include "m2model/AitdImage.h"
#include "m2model/AitdPak.h"

namespace m2suite {

namespace {

// How much of a file's head to read for identification. Big enough to walk
// a useful stretch of an AITD PAK offset table and to cover a backdrop's
// header, small enough that scanning a whole game folder stays fast.
constexpr int kSniffBytes = 512;

// Probes an AITD PAK for real 3D bodies. Nearly any blob can be coerced
// through the body parser, so require several entries that carry genuine
// geometry before calling the archive a model archive; this cleanly
// separates 4LSTBODY/LISTBODY (hundreds of bodies) from MASK/ANIM/ETAGE
// PAKs, which yield at most a couple of false positives.
bool aitdPakHoldsModels(const QString& path) {
    try {
        auto pak = m2model::AitdPak::openFromFile(path.toStdWString());
        size_t entries = pak.entryCount();
        if (entries < 4) {
            return false;
        }
        size_t probe = std::min<size_t>(entries, 24);
        size_t hits = 0;
        for (size_t i = 0; i < probe; ++i) {
            auto body = m2model::parseAitdBody(pak.read(i));
            if (body.valid && body.vertexCount() >= 4 && body.primitives.size() >= 2) {
                ++hits;
            }
        }
        return hits * 2 >= probe; // at least half the sampled entries
    } catch (const std::exception&) {
        return false;
    }
}

// Cheap structural test for an AITD PAK offset table, done entirely on the
// header bytes plus the file size: offsets[0] is 0, offsets[1] doubles as
// the table size, and every offset must be strictly increasing and inside
// the file. Reading a handful of table entries is enough to reject
// essentially anything that merely starts with four zero bytes, which
// keeps the (much more expensive) decompression probe off the hot path
// when scanning a whole game folder.
bool looksLikeAitdPakTable(const QString& path, const char* buf, qint64 n) {
    if (n < 16 || buf[0] || buf[1] || buf[2] || buf[3]) {
        return false;
    }
    auto rd32 = [&](qint64 o) {
        return quint32(quint8(buf[o])) | (quint32(quint8(buf[o + 1])) << 8) |
               (quint32(quint8(buf[o + 2])) << 16) | (quint32(quint8(buf[o + 3])) << 24);
    };
    quint32 tableSize = rd32(4);
    if (tableSize < 8 || tableSize % 4 != 0 || tableSize > 0x100000) {
        return false;
    }
    qint64 fileSize = QFileInfo(path).size();
    if (tableSize >= fileSize) {
        return false;
    }
    // Walk as many table entries as the header sample covers.
    qint64 avail = std::min<qint64>(n, tableSize);
    quint32 prev = tableSize;
    for (qint64 o = 8; o + 4 <= avail; o += 4) {
        quint32 off = rd32(o);
        if (off == 0) {
            break; // unused tail slots are zero-filled
        }
        if (off <= prev || off >= fileSize) {
            return false;
        }
        prev = off;
    }
    return true;
}

bool tagIs(const char* buf, const char* tag) {
    return buf[0] == tag[0] && buf[1] == tag[1] && buf[2] == tag[2] && buf[3] == tag[3];
}

// True when a 'CCB '-led file contains more than one cel, i.e. it's really
// a frame sequence. Walks the flat chunk chain (cel sizes include the
// 8-byte header) and stops as soon as a second CCB is seen, so this stays
// cheap even on large files.
bool isCelChainAnimation(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    qint64 fileSize = file.size();
    qint64 pos = 0;
    int ccbCount = 0;
    for (int guard = 0; guard < 64 && pos + 8 <= fileSize; ++guard) {
        if (!file.seek(pos)) {
            break;
        }
        unsigned char hdr[8];
        if (file.read(reinterpret_cast<char*>(hdr), 8) != 8) {
            break;
        }
        quint32 size = (quint32(hdr[4]) << 24) | (quint32(hdr[5]) << 16) |
                       (quint32(hdr[6]) << 8) | hdr[7];
        if (size < 8 || pos + qint64(size) > fileSize) {
            break; // malformed / trailing data
        }
        if (tagIs(reinterpret_cast<const char*>(hdr), "CCB ") && ++ccbCount > 1) {
            return true;
        }
        pos += size;
    }
    return false;
}
} // namespace

FileType sniffFileType(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return FileType::Unknown;
    }
    // Magic-byte checks need 12 bytes, but the structural probes need more:
    // an AITD PAK offset table is walked entry by entry, and a backdrop
    // header is 4 bytes followed by a 768-byte palette. This buffer used to
    // be 12 bytes, which meant *every* structural probe failed its own
    // length guard and silently returned false — no PAK and no backdrop was
    // ever recognised, however valid the file.
    char buf[kSniffBytes] = {};
    qint64 n = file.read(buf, kSniffBytes);
    const qint64 fileSize = file.size();
    file.close();

    const auto* ubuf = reinterpret_cast<const unsigned char*>(buf);
    if (n >= 4 && buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
        return FileType::Elf;
    }
    if (n >= 4 && tagIs(buf, "CCB ")) {
        // A file can be a chain of complete cels — that's an animation, not
        // a still (Yu Yu Hakusho's GRAPH/*.anim are 12 back-to-back
        // CCB+XTRA+PDAT groups). Walk the chunk chain far enough to see
        // whether a second CCB follows.
        if (isCelChainAnimation(path)) {
            return FileType::Anim;
        }
        return FileType::Cel;
    }
    if (n >= 4 && tagIs(buf, "ANIM")) {
        return FileType::Anim;
    }
    if (n >= 4 && tagIs(buf, "IMAG")) {
        return FileType::Imag;
    }
    if (n >= 4 && tagIs(buf, "SHDR")) {
        return FileType::StreamFile;
    }
    // Standalone 3DO film: 'FILM' + size + 'FDSC' descriptor chunk.
    if (n >= 12 && tagIs(buf, "FILM") && tagIs(buf + 8, "FDSC")) {
        return FileType::FilmFile;
    }
    // Some DataStreamer files pad before the header (Need For Speed's
    // Movies/*.Stream open with a FILL block), so a leading FILL still
    // means a stream — the loader scans forward for the SHDR.
    if (n >= 4 && tagIs(buf, "FILL")) {
        return FileType::StreamFile;
    }
    if (n >= 12 && tagIs(buf, "RIFF") && tagIs(buf + 8, "WAVE")) {
        return FileType::Wav;
    }
    // Standard image formats Qt can load directly.
    if (n >= 4 && buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F' && buf[3] == '8') {
        return FileType::StandardImage;
    }
    if (n >= 3 && ubuf[0] == 0xFF && ubuf[1] == 0xD8 && ubuf[2] == 0xFF) {
        return FileType::StandardImage; // JPEG
    }
    if (n >= 8 && ubuf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G') {
        return FileType::StandardImage;
    }
    if (n >= 2 && buf[0] == 'B' && buf[1] == 'M') {
        return FileType::StandardImage;
    }
    if (n >= 12 && (tagIs(buf, "FORM") || tagIs(buf, "CAT "))) {
        const char* type = buf + 8;
        if (tagIs(type, "TXTR")) return FileType::UtfTexture;
        // A FORM whose declared size leaves most of the file unaccounted
        // for is a catalogue of sounds, not one sound.
        if ((tagIs(type, "AIFF") || tagIs(type, "AIFC")) &&
            m2audio::looksLikeAiffCatalogue(reinterpret_cast<const uint8_t*>(buf), size_t(n),
                                             uint64_t(fileSize))) {
            return FileType::SoundCatalogue;
        }
        if (tagIs(type, "AIFF")) return FileType::Aiff;
        if (tagIs(type, "AIFC")) return FileType::Aifc;
        if (tagIs(type, "3INS")) return FileType::Instrument;
        if (tagIs(type, "FONT")) return FileType::Font;
        if (tagIs(type, "DDF ")) return FileType::Ddf;
        if (tagIs(type, "INTL")) return FileType::Intl;
    }
    if (n >= 12 && tagIs(buf + 8, "TAG0")) {
        if (tagIs(buf, "PEBM")) return FileType::Pebm;
        if (tagIs(buf, "M1VC")) return FileType::M1vc;
    }
    // 3DO resource bundle. Files with an .AIFF extension are sometimes
    // actually these (Road Rash's Rash.AIFF holds 19 sounds), so the magic
    // has to win over the extension.
    if (n >= 4 && tagIs(buf, "RSRC")) {
        return FileType::RsrcBundle;
    }
    // 3DO disc banner screen: a length byte then the 'APPSCRN' tag.
    if (n >= 8 && std::memcmp(buf + 1, "APPSCRN", 7) == 0) {
        return FileType::Banner;
    }

    // --- Alone in the Dark ------------------------------------------------
    // AITD backdrops (.pics/.bob/.pad) are raw paletted pages with no magic,
    // so they are identified structurally.
    if (m2model::looksLikeAitdImage(reinterpret_cast<const uint8_t*>(buf), size_t(n),
                                     uint64_t(fileSize))) {
        return FileType::AitdImage;
    }

    // AITD in-game book/document pages carry a plain-text magic.
    if (n >= 6 && std::memcmp(buf, "PAGES:", 6) == 0) {
        return FileType::AitdPages;
    }

    // AITD archives have no magic — they open with a little-endian offset
    // table whose first entry is 0 and whose second is the table size. A
    // sweep of both 3DO AITD games showed the body archives are exactly
    // LISTBODY/LISTBOD2 (AITD1, 272 models each) and the numbered
    // *LSTBODY.PAK (AITD2, 553); every other PAK holds anims, masks,
    // floors, scripts or samples.
    //
    // Detection is purely structural. Matching on the filename alone used
    // to claim anything *containing* a known archive name, so models
    // exported beside the archive — LISTBOD2.PAK_00000.ply — were reported
    // as AITD PAKs. The offset table is checked first; only then is the
    // name consulted, as a cheap shortcut past the decompression probe.
    if (looksLikeAitdPakTable(path, buf, n)) {
        QString base = path.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1).toUpper();
        // The name only classifies a file whose offset table already
        // validated, and only when it really is a .PAK — that ordering is
        // what stopped exported models being claimed as archives.
        if (base.endsWith(QStringLiteral(".PAK"))) {
            if (base.contains(QStringLiteral("LSTBODY")) ||
                base.contains(QStringLiteral("LISTBODY")) ||
                base.contains(QStringLiteral("LISTBOD2"))) {
                return FileType::AitdPak;
            }
            if (base.startsWith(QStringLiteral("ETAGE"))) {
                return FileType::AitdRooms;
            }
            if (base.startsWith(QStringLiteral("MASK")) ||
                base.startsWith(QStringLiteral("NASK"))) { // NASK11: a typo on disc
                return FileType::AitdMaskPak;
            }
            if (base.contains(QStringLiteral("LSTANIM")) ||
                base.contains(QStringLiteral("LISTANI")) ||
                base.startsWith(QStringLiteral("ANIM"))) {
                return FileType::AitdAnimPak;
            }
            if (base.contains(QStringLiteral("LISTSAMP")) ||
                base.contains(QStringLiteral("SAMP"))) {
                return FileType::AitdSoundPak;
            }
            if (base.contains(QStringLiteral("LSTLIFE")) ||
                base.contains(QStringLiteral("LSTTRAK")) ||
                base.contains(QStringLiteral("LSTMAT")) ||
                base.contains(QStringLiteral("LSTHYB")) ||
                base.contains(QStringLiteral("LISTLIFE")) ||
                base.contains(QStringLiteral("LISTTRAK"))) {
                return FileType::AitdScript;
            }
        }
        // Probing costs a few decompressions, so it runs only once the
        // offset table has already been validated against the real file
        // size — that gate rejects essentially everything that merely
        // happens to start with four zero bytes.
        return aitdPakHoldsModels(path) ? FileType::AitdPak : FileType::AitdArchive;
    }

    // 3DO M1 ARM executables have no magic. They begin with an ARM branch
    // or a MOV at the entry point; the reliable tell across the discs
    // sampled is the 0xE1/0xEA/0xEB condition-and-opcode byte in the top
    // byte of the first little-endian word, plus no extension.
    if (n >= 8 && (ubuf[3] == 0xE1 || ubuf[3] == 0xEA || ubuf[3] == 0xEB) &&
        !path.section(QLatin1Char('/'), -1).contains(QLatin1Char('.'))) {
        return FileType::ArmExecutable;
    }

    // Extension fallback for files whose magic didn't match (or short files).
    QString lower = path.toLower();
    // Export output, not game data. Game folders accumulate these once
    // people start converting things, and leaving them Unknown makes a
    // browse look broken.
    if (lower.endsWith(QStringLiteral(".obj")) || lower.endsWith(QStringLiteral(".ply")) ||
        lower.endsWith(QStringLiteral(".mtl")) || lower.endsWith(QStringLiteral(".gltf")) ||
        lower.endsWith(QStringLiteral(".glb"))) {
        return FileType::ExportedModel;
    }
    if (lower.endsWith(QStringLiteral(".16x"))) return FileType::AitdPages;
    if (lower.endsWith(QStringLiteral(".itd"))) return FileType::AitdData;
    if (lower.endsWith(QStringLiteral(".utf"))) return FileType::UtfTexture;
    if (lower.endsWith(QStringLiteral(".cel"))) return FileType::Cel;
    if (lower.endsWith(QStringLiteral(".anim")) || lower.endsWith(QStringLiteral(".anime"))) {
        return FileType::Anim;
    }
    if (lower.endsWith(QStringLiteral(".aiff")) || lower.endsWith(QStringLiteral(".aif"))) {
        return FileType::Aiff;
    }
    if (lower.endsWith(QStringLiteral(".aifc"))) return FileType::Aifc;
    if (lower.endsWith(QStringLiteral(".sc"))) return FileType::Aifc; // Yu Yu Hakusho sound
    if (lower.endsWith(QStringLiteral(".wav"))) return FileType::Wav;
    if (lower.endsWith(QStringLiteral(".str")) || lower.endsWith(QStringLiteral(".stream")) ||
        lower.endsWith(QStringLiteral(".cine"))) {
        return FileType::StreamFile;
    }
    if (lower.endsWith(QStringLiteral(".film")) || lower.endsWith(QStringLiteral(".movie"))) {
        return FileType::FilmFile;
    }
    if (lower.endsWith(QStringLiteral(".gif")) || lower.endsWith(QStringLiteral(".jpg")) ||
        lower.endsWith(QStringLiteral(".jpeg")) || lower.endsWith(QStringLiteral(".png")) ||
        lower.endsWith(QStringLiteral(".bmp"))) {
        return FileType::StandardImage;
    }
    // AITD odds and ends: backdrops that failed the structural check (a
    // truncated rip, say) are still worth labelling by extension.
    if (lower.endsWith(QStringLiteral(".pics")) || lower.endsWith(QStringLiteral(".bob")) ||
        lower.endsWith(QStringLiteral(".pad"))) {
        return FileType::AitdImage;
    }
    if (lower.endsWith(QStringLiteral(".stk")) || lower.endsWith(QStringLiteral(".txt"))) {
        return FileType::TextFile;
    }
    {
        QString base = path.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1).toUpper();
        if (base.startsWith(QStringLiteral("SAV"))) {
            return FileType::AitdSave;
        }
    }
    return FileType::Unknown;
}

QString fileTypeLabel(FileType type) {
    switch (type) {
        case FileType::UtfTexture: return QStringLiteral("Texture (UTF)");
        case FileType::Cel: return QStringLiteral("Image (CEL)");
        case FileType::Anim: return QStringLiteral("Animation (ANIM)");
        case FileType::Imag: return QStringLiteral("Image (IMAG)");
        case FileType::Aiff: return QStringLiteral("Audio (AIFF)");
        case FileType::Aifc: return QStringLiteral("Audio (AIFC)");
        case FileType::Wav: return QStringLiteral("Audio (WAV)");
        case FileType::StandardImage: return QStringLiteral("Image (GIF/JPG/PNG)");
        case FileType::StreamFile: return QStringLiteral("Stream (.str)");
        case FileType::Instrument: return QStringLiteral("Instrument (3INS)");
        case FileType::Font: return QStringLiteral("Font");
        case FileType::Ddf: return QStringLiteral("Device desc (DDF)");
        case FileType::Intl: return QStringLiteral("Locale (INTL)");
        case FileType::Pebm: return QStringLiteral("M2 bitmap (PEBM)");
        case FileType::M1vc: return QStringLiteral("M1 video (M1VC)");
        case FileType::Elf: return QStringLiteral("Executable (ELF)");
        case FileType::FilmFile: return QStringLiteral("Film (Cinepak)");
        case FileType::AitdPak: return QStringLiteral("3D models (AITD PAK)");
        case FileType::AitdRooms: return QStringLiteral("Rooms (AITD ETAGE)");
        case FileType::AitdImage: return QStringLiteral("Backdrop (AITD)");
        case FileType::AitdArchive: return QStringLiteral("Archive (AITD PAK)");
        case FileType::AitdAnimPak: return QStringLiteral("Animations (AITD PAK)");
        case FileType::AitdMaskPak: return QStringLiteral("Camera masks (AITD PAK)");
        case FileType::AitdSoundPak: return QStringLiteral("Sound archive (AITD PAK)");
        case FileType::AitdPages: return QStringLiteral("Document (AITD PAGES)");
        case FileType::AitdData: return QStringLiteral("Engine data (AITD ITD)");
        case FileType::AitdScript: return QStringLiteral("Scripts (AITD PAK)");
        case FileType::AitdSave: return QStringLiteral("Save game (AITD)");
        case FileType::RsrcBundle: return QStringLiteral("Resource bundle (RSRC)");
        case FileType::SoundCatalogue: return QStringLiteral("Sound catalogue (AIFF)");
        case FileType::Banner: return QStringLiteral("Banner screen (APPSCRN)");
        case FileType::ArmExecutable: return QStringLiteral("Executable (ARM, M1)");
        case FileType::TextFile: return QStringLiteral("Text");
        case FileType::ExportedModel: return QStringLiteral("Exported model");
        case FileType::Unknown: return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

FileCategory fileTypeCategory(FileType type) {
    switch (type) {
        case FileType::UtfTexture:
            return FileCategory::Textures;

        case FileType::Cel:
        case FileType::Imag:
        case FileType::StandardImage:
        case FileType::Pebm:
        case FileType::AitdImage:
        case FileType::Banner:
            return FileCategory::Images;

        case FileType::Anim:
            return FileCategory::Animation;

        case FileType::StreamFile:
        case FileType::FilmFile:
        case FileType::M1vc:
            return FileCategory::Video;

        case FileType::Aiff:
        case FileType::Aifc:
        case FileType::Wav:
        case FileType::RsrcBundle:
        case FileType::SoundCatalogue:
        case FileType::Instrument:
            return FileCategory::Audio;

        case FileType::AitdPak:
        case FileType::AitdRooms:
        case FileType::ExportedModel:
            return FileCategory::Models3D;

        case FileType::AitdArchive:
        case FileType::AitdAnimPak:
        case FileType::AitdMaskPak:
        case FileType::AitdSoundPak:
            return FileCategory::Archives;

        case FileType::Elf:
        case FileType::ArmExecutable:
            return FileCategory::Executables;

        case FileType::Font:
        case FileType::Ddf:
        case FileType::Intl:
        case FileType::AitdPages:
        case FileType::AitdData:
        case FileType::AitdScript:
        case FileType::AitdSave:
        case FileType::TextFile:
            return FileCategory::Data;

        case FileType::Unknown:
            return FileCategory::Unrecognised;
    }
    return FileCategory::Unrecognised;
}

QString fileCategoryLabel(FileCategory category) {
    switch (category) {
        case FileCategory::All: return QStringLiteral("All types");
        case FileCategory::Textures: return QStringLiteral("Textures");
        case FileCategory::Images: return QStringLiteral("Images");
        case FileCategory::Animation: return QStringLiteral("Animation");
        case FileCategory::Video: return QStringLiteral("Video & streams");
        case FileCategory::Audio: return QStringLiteral("Audio");
        case FileCategory::Models3D: return QStringLiteral("3D models & rooms");
        case FileCategory::Archives: return QStringLiteral("Archives");
        case FileCategory::Executables: return QStringLiteral("Executables");
        case FileCategory::Data: return QStringLiteral("Data & documents");
        case FileCategory::Unrecognised: return QStringLiteral("Unrecognised");
    }
    return QStringLiteral("All types");
}

const QList<FileCategory>& fileCategoryOrder() {
    static const QList<FileCategory> order{
        FileCategory::All,        FileCategory::Textures,    FileCategory::Images,
        FileCategory::Animation,  FileCategory::Video,       FileCategory::Audio,
        FileCategory::Models3D,   FileCategory::Archives,    FileCategory::Executables,
        FileCategory::Data,       FileCategory::Unrecognised,
    };
    return order;
}

bool isAitdArchiveType(FileType type) {
    switch (type) {
        case FileType::AitdPak:
        case FileType::AitdRooms:
        case FileType::AitdArchive:
        case FileType::AitdAnimPak:
        case FileType::AitdMaskPak:
        case FileType::AitdSoundPak:
        case FileType::AitdScript:
            return true;
        default:
            return false;
    }
}

bool fileTypeHasPreview(FileType type) {
    switch (type) {
        case FileType::UtfTexture:
        case FileType::Cel:
        case FileType::Anim:
        case FileType::Imag:
        case FileType::Aiff:
        case FileType::Aifc:
        case FileType::Wav:
        case FileType::StandardImage:
        case FileType::StreamFile:
        case FileType::FilmFile:   // standalone Cinepak film
        case FileType::AitdImage:  // paletted backdrop pages
        case FileType::AitdPak:    // interactive 3D model view
        case FileType::AitdRooms:  // room archives, browsed the same way
        case FileType::AitdArchive:
        case FileType::AitdAnimPak:
        case FileType::AitdMaskPak:
        case FileType::AitdSoundPak:
        case FileType::AitdScript:
        case FileType::Elf:        // disassembly view
        case FileType::M1vc:       // MPEG video in a TAG0 container
        case FileType::Ddf:        // IFF FORM shown as structured text
        case FileType::Intl:       // locale FORM shown as text
        case FileType::Instrument:     // DSP 3INS structure view
        case FileType::SoundCatalogue: // browse and play each sound
            return true;
        default:
            return false;
    }
}

} // namespace m2suite
