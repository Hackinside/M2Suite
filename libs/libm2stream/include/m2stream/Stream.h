#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace m2stream {

// 3DO DataStreamer file (.str / .stream / .mov on M2 discs): an SHDR
// stream header followed by quad-aligned chunks, each with a 20-byte
// common header {type, size, time, channel, subType}. Subscriber data
// seen in real games:
//   FILM/FHDR+FRME — Cinepak video (M1-era, e.g. strahl)
//   SNDS/SHDR+SSMP — SDX2-compressed audio
//   MPVD/VHDR+FRAM — MPEG-1 video elementary stream (M2-era)
//   MPAU/…+FRAM    — MPEG-1 audio elementary stream
// Layouts verified against CPakSubscriber.h (CinepackSub reference) and
// hex inspection of strahl/Oldsmobile/imsaM2 stream files.
class Stream {
public:
    static Stream load(const uint8_t* data, size_t size);
    static Stream loadFromFile(const std::filesystem::path& path);
    // Standalone QuickTime-derived 3DO film ('FILM'/'FDSC'/'STAB', e.g. Yu
    // Yu Hakusho's MOVIE/*.film). Parsed into the same film/audio fields a
    // DataStreamer movie uses; load() dispatches here automatically.
    static Stream loadStandaloneFilm(const uint8_t* data, size_t size);

    struct FilmInfo {
        bool valid = false;
        uint32_t cType = 0; // compression fourcc, 'cvid' for Cinepak
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t scale = 0; // timescale (ticks per second)
        uint32_t count = 0; // frame count per FHDR
    };
    struct FilmFrame {
        uint32_t time = 0;
        uint32_t duration = 0; // in FilmInfo::scale ticks
        std::vector<uint8_t> data;
    };
    struct AudioInfo {
        bool valid = false;
        uint32_t sampleSizeBits = 0;
        uint32_t sampleRate = 0;
        uint32_t channels = 0;
        uint32_t compression = 0; // fourcc, e.g. 'SDX2'
        uint32_t compressionRatio = 0;
        uint32_t sampleCount = 0;
    };

    const std::vector<uint32_t>& subscribers() const { return subscribers_; }

    const FilmInfo& film() const { return film_; }
    const std::vector<FilmFrame>& filmFrames() const { return filmFrames_; }

    const AudioInfo& audio() const { return audio_; }
    // Concatenated raw SSMP sample bytes (SDX2-compressed if
    // audio().compression == 'SDX2').
    const std::vector<uint8_t>& audioData() const { return audioData_; }

    // Concatenated MPEG-1 elementary streams from FRAM chunks — playable
    // by any MPEG-capable player once written to a file.
    const std::vector<uint8_t>& mpegVideo() const { return mpegVideo_; }
    const std::vector<uint8_t>& mpegAudio() const { return mpegAudio_; }

    bool hasFilm() const { return film_.valid && !filmFrames_.empty(); }
    bool hasAudio() const { return audio_.valid && !audioData_.empty(); }
    bool hasMpegVideo() const { return !mpegVideo_.empty(); }
    bool hasMpegAudio() const { return !mpegAudio_.empty(); }

private:
    std::vector<uint32_t> subscribers_;
    FilmInfo film_;
    std::vector<FilmFrame> filmFrames_;
    AudioInfo audio_;
    std::vector<uint8_t> audioData_;
    std::vector<uint8_t> mpegVideo_;
    std::vector<uint8_t> mpegAudio_;
};

// Extracts the payload of the 'DATA' chunk from a TAG0-style container
// (M1VC video, PEBM bitmap, ... as used by Oldsmobile's browser media:
// <FOURCC> <size> 'TAG0' <size> <16-byte guid> 'DATA' <size> <payload>).
// Returns an empty vector if the file isn't such a container.
// Containers can nest (Pontiac's ControlCenterUP.m1c wraps its MPEG in six
// M1VC/TAG0 levels); `depth` is the internal recursion guard.
std::vector<uint8_t> extractTag0Data(const uint8_t* data, size_t size, int depth = 0);

// Decodes one Cinepak ('cvid') frame sequence. Wraps the bundled decoder
// by Dr. Tim Ferguson (third_party/cinepak_decode.cpp — free to use with
// attribution; see that file's header). Frames must be decoded in order:
// Cinepak is inter-frame compressed and the context carries state.
class CinepakDecoder {
public:
    CinepakDecoder();
    ~CinepakDecoder();
    CinepakDecoder(const CinepakDecoder&) = delete;
    CinepakDecoder& operator=(const CinepakDecoder&) = delete;

    // Decodes into a caller-provided RGBA8 buffer of width*height*4 bytes.
    // Returns the underlying decoder's status (0 = ok; nonzero = frame
    // skipped/partial — buffer keeps previous contents).
    //
    // Cinepak works in 4x4 blocks; for dimensions that aren't multiples of
    // 4 (e.g. strahl's 182x137 films) the decoder writes whole blocks past
    // the nominal edges, so decoding happens into an internal padded
    // buffer and the visible region is copied out — callers never need to
    // over-allocate.
    int decodeFrame(const uint8_t* data, size_t size, uint8_t* rgbaOut,
                     uint32_t width, uint32_t height);

private:
    void* ctx_ = nullptr;
    std::vector<uint8_t> padded_; // block-aligned scratch target
};

} // namespace m2stream
