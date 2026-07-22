#include "m2stream/Stream.h"

#include <fstream>
#include <string>

#include "m2core/ByteStream.h"
#include "m2core/Error.h"
#include "m2core/Iff.h"

#include "../third_party/cinepak_decode.h"

namespace m2stream {

using m2core::ByteReader;
using m2core::FormatError;

namespace {
constexpr uint32_t ID_SHDR = m2core::makeId('S', 'H', 'D', 'R');
constexpr uint32_t ID_FILM = m2core::makeId('F', 'I', 'L', 'M');
constexpr uint32_t ID_SNDS = m2core::makeId('S', 'N', 'D', 'S');
constexpr uint32_t ID_MPVD = m2core::makeId('M', 'P', 'V', 'D');
constexpr uint32_t ID_MPVC = m2core::makeId('M', 'P', 'V', 'C'); // MPEG video variant tag
constexpr uint32_t ID_MPAU = m2core::makeId('M', 'P', 'A', 'U');
constexpr uint32_t ID_FILL = m2core::makeId('F', 'I', 'L', 'L');
constexpr uint32_t ID_CTRL = m2core::makeId('C', 'T', 'R', 'L');
constexpr uint32_t ID_TAG0_TOP = m2core::makeId('T', 'A', 'G', '0');
constexpr uint32_t SUB_FHDR = m2core::makeId('F', 'H', 'D', 'R');
constexpr uint32_t SUB_SHDR = m2core::makeId('S', 'H', 'D', 'R');
constexpr uint32_t SUB_SSMP = m2core::makeId('S', 'S', 'M', 'P');
constexpr uint32_t SUB_FRME = m2core::makeId('F', 'R', 'M', 'E');
// 'DFRM' = difference frame: structurally identical to FRME (same 8-byte
// header then cvid data, just with the inter-frame flag set in the cvid
// header). Coven's Intro.stream is 96% DFRM — ignoring them left only the
// 242 keyframes and made playback unwatchable.
constexpr uint32_t SUB_DFRM = m2core::makeId('D', 'F', 'R', 'M');

uint32_t u32be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// MPVD video frame subtypes vary by encoder/frame kind — observed 'FRAM',
// 'FRM[', 'FRM]', plus 'FRME' from the SDK chunkifier. All are video ES
// carriers, distinguished only by leading "FR". (Verified against
// Oldsmobile 41090.stream and the ws_root MPEGVideoChunkifier source.)
bool isVideoFrameSubtype(uint32_t sub) {
    return (sub >> 16) == (('F' << 8) | 'R');
}

// 'FRAM' and 'FRM[' payloads open with a 4-byte pts; 'FRM]' is the raw
// continuation/tail of a frame split across chunks and has NO prefix —
// stripping 4 bytes from those was corrupting the elementary stream at
// every split point (150 spots in Oldsmobile4's 72130.stream = the
// reported artifacts). Verified by hex: FRAM/FRM[ show pts then an MPEG
// start code; FRM] starts mid-slice-data.
bool videoFrameHasPtsPrefix(uint32_t sub) {
    return uint8_t(sub & 0xFF) != ']';
}

// MPAU audio frame subtypes are '1FRM'..'4FRM' by MPEG layer; 'MHDR' is
// the stream header. (ws_root dsstreamdefs.h.)
bool isAudioFrameSubtype(uint32_t sub) {
    uint8_t c0 = uint8_t(sub >> 24);
    return c0 >= '1' && c0 <= '4' && (sub & 0x00FFFFFF) == (('F' << 16) | ('R' << 8) | 'M');
}
} // namespace

namespace {
// Standalone 3DO/Sega "FILM" movie (Yu Yu Hakusho's MOVIE/*.film): a
// QuickTime-derived container that is NOT a DataStreamer file — no SHDR,
// no 20-byte subscriber chunks. Layout per FFmpeg's segafilm demuxer:
//   'FILM' dataOffset version reserved            (16 bytes)
//   'FDSC' size codec('cvid') height width ...
//   'STAB' size baseClock sampleCount, then
//          sampleCount x { offset, size, info1, info2 }  (16 bytes each)
// A sample whose info1 == 0xFFFFFFFF is audio; otherwise it's a video
// frame with pts = info1 & 0x7FFFFFFF. Sample offsets are relative to
// dataOffset. Parsed into the same fields a DataStreamer film uses, so
// playback/export paths need no special case.
constexpr uint32_t ID_FDSC = m2core::makeId('F', 'D', 'S', 'C');
constexpr uint32_t ID_STAB = m2core::makeId('S', 'T', 'A', 'B');
constexpr uint32_t ID_CVID = m2core::makeId('c', 'v', 'i', 'd');
constexpr uint32_t ID_NONE_FOURCC = m2core::makeId('N', 'O', 'N', 'E');

bool looksLikeStandaloneFilm(const uint8_t* data, size_t size) {
    return size >= 24 && u32be(data) == ID_FILM && u32be(data + 16) == ID_FDSC;
}
} // namespace

Stream Stream::loadStandaloneFilm(const uint8_t* data, size_t size) {
    Stream s;
    uint32_t dataOffset = u32be(data + 4);
    uint32_t version = u32be(data + 8);

    // FDSC at +16: id, size, codec, height, width.
    uint32_t fdscSize = u32be(data + 20);
    if (fdscSize < 20 || size_t(16) + fdscSize > size) {
        throw FormatError("FILM: bad FDSC chunk size");
    }
    uint32_t codec = u32be(data + 24);
    uint32_t height = u32be(data + 28);
    uint32_t width = u32be(data + 32);
    if (codec != ID_CVID) {
        throw FormatError("FILM: only Cinepak ('cvid') video is supported");
    }

    // STAB follows FDSC.
    size_t stabPos = 16 + fdscSize;
    if (stabPos + 16 > size || u32be(data + stabPos) != ID_STAB) {
        throw FormatError("FILM: missing STAB sample table");
    }
    uint32_t baseClock = u32be(data + stabPos + 8);
    uint32_t sampleCount = u32be(data + stabPos + 12);

    s.film_.valid = true;
    s.film_.cType = codec;
    s.film_.width = width;
    s.film_.height = height;
    s.film_.scale = baseClock;

    // The header carries no audio descriptor. FFmpeg's segafilm demuxer
    // assumes raw signed 8-bit PCM here (its Saturn/PC targets), but on 3DO
    // these films use SDX2 — one byte per sample, so the duration matches
    // 8-bit PCM either way, yet decoding as PCM is pure noise. Measured on
    // Yu Yu Hakusho's MV_01.film: SDX2 gives 0.92 sample autocorrelation vs
    // 0.01 for PCM. Treat FILM audio as SDX2 mono 22050.
    s.audio_.valid = true;
    s.audio_.sampleSizeBits = 16;
    s.audio_.sampleRate = 22050;
    s.audio_.channels = 1;
    s.audio_.compression = m2core::makeId('S', 'D', 'X', '2');
    (void)version;

    size_t entry = stabPos + 16;
    for (uint32_t i = 0; i < sampleCount && entry + 16 <= size; ++i, entry += 16) {
        uint32_t off = u32be(data + entry);
        uint32_t len = u32be(data + entry + 4);
        uint32_t info1 = u32be(data + entry + 8);
        size_t abs = size_t(dataOffset) + off;
        if (abs + len > size) {
            continue; // truncated dump — skip rather than abort
        }
        if (info1 == 0xFFFFFFFFu) {
            s.audioData_.insert(s.audioData_.end(), data + abs, data + abs + len);
        } else {
            FilmFrame f;
            f.time = info1 & 0x7FFFFFFFu;
            f.duration = u32be(data + entry + 12);
            f.data.assign(data + abs, data + abs + len);
            s.filmFrames_.push_back(std::move(f));
        }
    }
    s.film_.count = uint32_t(s.filmFrames_.size());
    if (s.audioData_.empty()) {
        s.audio_.valid = false;
    }
    return s;
}

Stream Stream::load(const uint8_t* data, size_t size) {
    if (size < 20) {
        throw FormatError("not a DataStreamer file: too small");
    }

    if (looksLikeStandaloneFilm(data, size)) {
        return loadStandaloneFilm(data, size);
    }

    // The stream header usually leads the file, but some discs pad first —
    // Need For Speed's Movies/*.Stream open with a FILL block, which made a
    // strict "SHDR at offset 0" check reject the whole file. Scan the chunk
    // chain for the header instead, and accept a stream that has none as
    // long as it carries recognisable subscriber chunks.
    size_t shdrPos = size_t(-1);
    {
        // Walk the opening chunks looking for the header. A stream is
        // accepted with no SHDR as long as it's a genuine chain of
        // DataStreamer chunks — real files lead with SHDR, FILL padding
        // (Need For Speed's Movies/1.1.Stream), or straight into content
        // chunks like CTRL/FILM/SNDS (NFS's 18.1.Stream starts with CTRL).
        size_t scan = 0;
        int payloadChunks = 0;
        while (scan + 20 <= size && scan < 0x20000) {
            uint32_t id = u32be(data + scan);
            uint32_t sz = u32be(data + scan + 4);
            if (sz < 8 || scan + sz > size + 3) {
                break;
            }
            if (id == ID_SHDR) {
                shdrPos = scan;
                break;
            }
            if (id != ID_FILL && id != ID_FILM && id != ID_SNDS && id != ID_CTRL &&
                id != ID_MPVD && id != ID_MPAU && id != ID_TAG0_TOP) {
                break; // not a DataStreamer chunk chain
            }
            if (id == ID_FILM || id == ID_SNDS || id == ID_MPVD || id == ID_MPAU) {
                ++payloadChunks; // real subscriber content, not just padding
            }
            scan += (size_t(sz) + 3) & ~size_t(3);
        }
        if (shdrPos == size_t(-1) && payloadChunks == 0) {
            throw FormatError("not a DataStreamer file: no SHDR header");
        }
    }

    Stream s;

    // Subscriber table lives at fixed offset 0x74 inside the SHDR chunk:
    // {fourcc, priority} pairs until a zero tag (verified across strahl /
    // Oldsmobile / imsaM2 stream headers).
    if (shdrPos != size_t(-1)) {
        uint32_t shdrSize = u32be(data + shdrPos + 4);
        for (size_t off = shdrPos + 0x74;
             off + 8 <= shdrPos + shdrSize && off + 8 <= size; off += 8) {
            uint32_t tag = u32be(data + off);
            if (tag == 0) {
                break;
            }
            s.subscribers_.push_back(tag);
        }
    }

    // Chunk walk. Every chunk: type(4) size(4) time(4) channel(4)
    // subType(4) payload[size-20]; sizes are quad-aligned in the file even
    // when the stored size isn't.
    size_t pos = 0;
    while (pos + 20 <= size) {
        uint32_t type = u32be(data + pos);
        uint32_t chunkSize = u32be(data + pos + 4);
        if (chunkSize < 8 || pos + chunkSize > size + 3) {
            break; // trailing garbage / padding — stop cleanly
        }
        uint32_t time = u32be(data + pos + 8);
        uint32_t subType = u32be(data + pos + 16);
        const uint8_t* payload = data + pos + 20;
        size_t payloadLen = (chunkSize > 20) ? std::min<size_t>(chunkSize - 20, size - pos - 20) : 0;

        if (type == ID_FILM && subType == SUB_FHDR && payloadLen >= 24) {
            // CinePakHeader (CPakSubscriber.h): version, cType, height,
            // width, scale, count.
            s.film_.valid = true;
            s.film_.cType = u32be(payload + 4);
            s.film_.height = u32be(payload + 8);
            s.film_.width = u32be(payload + 12);
            s.film_.scale = u32be(payload + 16);
            s.film_.count = u32be(payload + 20);
        } else if (type == ID_FILM && (subType == SUB_FRME || subType == SUB_DFRM) &&
                    payloadLen >= 8) {
            // CinePakFrame: duration, frameSize, frameData. frameSize
            // counts from the duration field, so the compressed data is
            // frameSize - 8 bytes (CelLib.cpp passes exactly that to
            // decode_cinepak).
            FilmFrame frame;
            frame.time = time;
            frame.duration = u32be(payload);
            uint32_t frameSize = u32be(payload + 4);
            size_t dataLen = (frameSize > 8) ? frameSize - 8 : 0;
            size_t avail = payloadLen - 8;
            frame.data.assign(payload + 8, payload + 8 + std::min<size_t>(dataLen, avail));
            s.filmFrames_.push_back(std::move(frame));
        } else if (type == ID_SNDS && subType == SUB_SHDR && payloadLen >= 44) {
            // SAudioHeaderChunk: version, numBuffers, initialAmplitude,
            // initialPan, then SAudioSampleDescriptor {dataFormat,
            // sampleSize, sampleRate, numChannels, compressionType,
            // compressionRatio, sampleCount} (verified by hex against
            // strahl: 16-bit, 22050 Hz, mono, SDX2, ratio 2).
            s.audio_.valid = true;
            s.audio_.sampleSizeBits = u32be(payload + 20);
            s.audio_.sampleRate = u32be(payload + 24);
            s.audio_.channels = u32be(payload + 28);
            s.audio_.compression = u32be(payload + 32);
            s.audio_.compressionRatio = u32be(payload + 36);
            s.audio_.sampleCount = u32be(payload + 40);
        } else if (type == ID_SNDS && subType == SUB_SSMP && payloadLen >= 4) {
            // SAudioDataChunk: actualSampleSize then sample bytes.
            uint32_t actual = u32be(payload);
            size_t n = std::min<size_t>(actual, payloadLen - 4);
            s.audioData_.insert(s.audioData_.end(), payload + 4, payload + 4 + n);
        } else if ((type == ID_MPVD || type == ID_MPVC) &&
                    isVideoFrameSubtype(subType) && payloadLen > 4) {
            // MPEG video frame: a 4-byte prefix (pts / temporal ref) then
            // raw MPEG-1 video ES (sequence header 00 00 01 B3 or picture
            // start 00 00 01 00 at payload+4). Includes all FR* frame
            // subtypes (FRAM / FRM[ / FRM]) — dropping the variants was
            // what caused the video artifacts.
            size_t skip = videoFrameHasPtsPrefix(subType) ? 4 : 0;
            s.mpegVideo_.insert(s.mpegVideo_.end(), payload + skip, payload + payloadLen);
        } else if (type == ID_MPAU && isAudioFrameSubtype(subType) && payloadLen > 0) {
            // MPEG audio frame: the payload IS the raw MPEG audio ES,
            // starting at the frame sync (0xFFE/0xFFF) with NO prefix —
            // stripping 4 bytes here (as video needs) is what silenced the
            // audio. 1FRM/2FRM/... encode the MPEG layer.
            s.mpegAudio_.insert(s.mpegAudio_.end(), payload, payload + payloadLen);
        }
        // SHDR / FILL / CTRL / TAG0 / headers of MPVD (VHDR) etc.: skipped.

        pos += (size_t(chunkSize) + 3) & ~size_t(3);
    }

    // Double-buffered films: some discs (Policenauts .film/.movie) carry
    // TWO interleaved copies of the movie — two FHDRs and exactly twice as
    // many FRME chunks as the header's frame count, alternating buffer-A /
    // buffer-B substreams. Each substream is self-consistent Cinepak;
    // decoding them interleaved corrupts both (scattered block noise). The
    // real CPak subscriber feeds one buffer chain — keep the even-index
    // substream.
    if (s.film_.valid && s.film_.count > 0 && s.filmFrames_.size() == size_t(s.film_.count) * 2) {
        std::vector<FilmFrame> single;
        single.reserve(s.film_.count);
        for (size_t i = 0; i < s.filmFrames_.size(); i += 2) {
            single.push_back(std::move(s.filmFrames_[i]));
        }
        s.filmFrames_ = std::move(single);
    }

    return s;
}

Stream Stream::loadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw FormatError("could not open file: " + path.string());
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    return load(bytes.data(), bytes.size());
}

namespace {
// True when a buffer looks like a <FOURCC><size>'TAG0'... container.
bool isTag0Container(const uint8_t* data, size_t size) {
    constexpr uint32_t ID_TAG0 = m2core::makeId('T', 'A', 'G', '0');
    return size >= 12 && u32be(data + 8) == ID_TAG0;
}
} // namespace

std::vector<uint8_t> extractTag0Data(const uint8_t* data, size_t size, int depth) {
    // Walk top-level chunks (type(4) size(4) then body) looking for 'DATA'.
    // The container's own outer id and the 'TAG0' guid chunk are skipped by
    // the generic walk. Sizes here do NOT include the 8-byte header (unlike
    // the cel format), verified against Oldsmobile's M1VC/PEBM files.
    constexpr uint32_t ID_DATA = m2core::makeId('D', 'A', 'T', 'A');
    if (size < 8) {
        return {};
    }
    // Skip the 8-byte outer container header (e.g. 'M1VC' + total size).
    size_t pos = 8;
    while (pos + 8 <= size) {
        uint32_t id = u32be(data + pos);
        uint32_t chunkSize = u32be(data + pos + 4);
        size_t body = pos + 8;
        if (body + chunkSize > size) {
            chunkSize = uint32_t(size - body);
        }
        if (id == ID_DATA) {
            // Containers nest: Pontiac's ControlCenterUP.m1c wraps its
            // payload in SIX levels of M1VC/TAG0/DATA before the real
            // MPEG-1 sequence header (00 00 01 B3, 640x480). Unwrap until
            // the payload is no longer a container; the depth cap stops a
            // malformed/cyclic file from blowing the stack.
            const uint8_t* inner = data + body;
            if (depth < 32 && isTag0Container(inner, chunkSize)) {
                return extractTag0Data(inner, chunkSize, depth + 1);
            }
            return std::vector<uint8_t>(inner, inner + chunkSize);
        }
        pos = body + ((chunkSize + 3) & ~size_t(3));
    }
    return {};
}

CinepakDecoder::CinepakDecoder() : ctx_(decode_cinepak_init()) {}

CinepakDecoder::~CinepakDecoder() {
    if (ctx_) {
        decode_cinepak_free(ctx_);
    }
}

int CinepakDecoder::decodeFrame(const uint8_t* data, size_t size, uint8_t* rgbaOut,
                                 uint32_t width, uint32_t height) {
    // Cinepak writes whole 4x4 blocks: give the decoder extra rows/slack
    // so non-multiple-of-4 dimensions (strahl: 182x137) can't overflow —
    // this was a hard crash before. Stride must stay `width` for correct
    // layout, so pad rows (+4) and add one row of tail slack.
    size_t paddedSize = size_t(width) * (height + 8) * 4;
    if (padded_.size() < paddedSize) {
        padded_.resize(paddedSize);
    }
    int ret = decode_cinepak(ctx_, const_cast<unsigned char*>(data), int(size),
                              padded_.data(), int(width), int(height), 32);

    // The Ferguson decoder writes B,G,R,- order for 32bpp output; callers
    // get RGBA by post-swizzling here so the rest of the suite stays RGBA8.
    size_t pixels = size_t(width) * height;
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t* src = padded_.data() + i * 4;
        uint8_t* dst = rgbaOut + i * 4;
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        dst[3] = 255;
    }
    return ret;
}

} // namespace m2stream
