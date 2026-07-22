// Parses real DataStreamer fixtures: sample.str (strahl, FILM Cinepak +
// SNDS SDX2) and sample.stream (Oldsmobile, MPVD MPEG video). Decodes the
// first Cinepak frame and the SDX2 audio, and checks the MPEG elementary
// stream starts with a valid MPEG start code.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "m2audio/Sdx2.h"
#include "m2stream/Stream.h"

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, \
                          __LINE__);                                            \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

using namespace m2stream;

static void testCinepakStream(const char* path) {
    Stream s = Stream::loadFromFile(path);
    CHECK(!s.subscribers().empty());

    if (s.hasFilm()) {
        const auto& f = s.film();
        CHECK(f.width > 0 && f.width <= 1024);
        CHECK(f.height > 0 && f.height <= 1024);
        CHECK(!s.filmFrames().empty());

        CinepakDecoder dec;
        std::vector<uint8_t> rgba(size_t(f.width) * f.height * 4, 0);
        const auto& frame0 = s.filmFrames().front();
        dec.decodeFrame(frame0.data.data(), frame0.data.size(), rgba.data(), f.width,
                         f.height);
        // A decoded frame must not be all-black.
        bool nonZero = false;
        for (size_t i = 0; i < rgba.size(); i += 4) {
            if (rgba[i] || rgba[i + 1] || rgba[i + 2]) {
                nonZero = true;
                break;
            }
        }
        CHECK(nonZero);
        std::printf("libm2stream_tests: cinepak %ux%u, %zu frames, frame 0 decoded\n",
                     f.width, f.height, s.filmFrames().size());
    }
    if (s.hasAudio()) {
        auto pcm = m2audio::decodeSdx2(s.audioData().data(), s.audioData().size(),
                                         s.audio().channels);
        CHECK(!pcm.empty());
        bool nonZero = false;
        for (int16_t v : pcm) {
            if (v != 0) {
                nonZero = true;
                break;
            }
        }
        CHECK(nonZero);
        std::printf("libm2stream_tests: SDX2 audio %u Hz, %zu samples decoded\n",
                     s.audio().sampleRate, pcm.size());
    }
    CHECK(s.hasFilm() || s.hasAudio());
}

static void testMpegStream(const char* path) {
    Stream s = Stream::loadFromFile(path);
    CHECK(s.hasMpegVideo());
    const auto& es = s.mpegVideo();
    // MPEG-1 sequence header start code 00 00 01 B3.
    CHECK(es.size() > 4);
    CHECK(es[0] == 0x00 && es[1] == 0x00 && es[2] == 0x01 && es[3] == 0xB3);
    std::printf("libm2stream_tests: MPEG ES %zu bytes, valid sequence header\n", es.size());
}

// Diagnostic mode: --scan <file> prints film details and decodes every
// frame, reporting how many come out (nearly) black; --dumpmpeg <file>
// <out> writes the extracted MPEG ES for external validation (ffprobe).
static int scanStream(const char* path) {
    Stream s = Stream::loadFromFile(path);
    if (!s.hasFilm()) {
        std::printf("%s: no film (audio=%d mpegV=%d mpegA=%d)\n", path, s.hasAudio(),
                     s.hasMpegVideo(), s.hasMpegAudio());
        return 0;
    }
    const auto& f = s.film();
    char ctype[5] = {char(f.cType >> 24), char(f.cType >> 16), char(f.cType >> 8),
                      char(f.cType), 0};
    std::printf("%s: cType='%s' %ux%u scale=%u count=%u frames=%zu\n", path, ctype, f.width,
                 f.height, f.scale, f.count, s.filmFrames().size());

    CinepakDecoder dec;
    std::vector<uint8_t> rgba(size_t(f.width) * f.height * 4, 0);
    size_t black = 0;
    int firstRet = -999;
    for (const auto& fr : s.filmFrames()) {
        int ret = dec.decodeFrame(fr.data.data(), fr.data.size(), rgba.data(), f.width, f.height);
        if (firstRet == -999) {
            firstRet = ret;
            std::printf("  frame0: decode ret=%d, dataSize=%zu, first bytes: %02x %02x %02x %02x\n",
                         ret, fr.data.size(), fr.data[0], fr.data[1], fr.data[2], fr.data[3]);
        }
        size_t lit = 0;
        for (size_t i = 0; i < rgba.size(); i += 16) { // sample every 4th px
            if (rgba[i] > 16 || rgba[i + 1] > 16 || rgba[i + 2] > 16) ++lit;
        }
        if (lit < rgba.size() / 16 / 20) ++black; // <5% lit pixels
    }
    std::printf("  black frames: %zu / %zu\n", black, s.filmFrames().size());
    return 0;
}

static int dumpFrame(const char* path, size_t frameIndex, const char* out) {
    Stream s = Stream::loadFromFile(path);
    if (!s.hasFilm() || frameIndex >= s.filmFrames().size()) return 1;
    const auto& f = s.film();
    CinepakDecoder dec;
    std::vector<uint8_t> rgba(size_t(f.width) * f.height * 4, 0);
    for (size_t i = 0; i <= frameIndex; ++i) { // sequential: inter-frame codec
        const auto& fr = s.filmFrames()[i];
        dec.decodeFrame(fr.data.data(), fr.data.size(), rgba.data(), f.width, f.height);
    }
    FILE* fp = std::fopen(out, "wb");
    if (!fp) return 1;
    std::fprintf(fp, "P6\n%u %u\n255\n", f.width, f.height);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        std::fwrite(&rgba[i], 1, 3, fp);
    }
    std::fclose(fp);
    std::printf("wrote frame %zu to %s\n", frameIndex, out);
    return 0;
}

static int dumpMpeg(const char* path, const char* out) {
    Stream s = Stream::loadFromFile(path);
    FILE* fp = std::fopen(out, "wb");
    if (!fp) return 1;
    if (s.hasMpegVideo()) {
        std::fwrite(s.mpegVideo().data(), 1, s.mpegVideo().size(), fp);
    }
    std::fclose(fp);
    std::printf("wrote %zu bytes video to %s (audio: %zu bytes)\n", s.mpegVideo().size(), out,
                 s.mpegAudio().size());
    return 0;
}

static int dumpMpegAudio(const char* path, const char* out) {
    Stream s = Stream::loadFromFile(path);
    FILE* fp = std::fopen(out, "wb");
    if (!fp) return 1;
    if (s.hasMpegAudio()) {
        std::fwrite(s.mpegAudio().data(), 1, s.mpegAudio().size(), fp);
    }
    std::fclose(fp);
    std::printf("wrote %zu bytes audio to %s\n", s.mpegAudio().size(), out);
    return 0;
}

int main(int argc, char** argv) {
    try {
        if (argc > 2 && std::string(argv[1]) == "--scan") {
            return scanStream(argv[2]);
        }
        if (argc > 3 && std::string(argv[1]) == "--dumpmpeg") {
            return dumpMpeg(argv[2], argv[3]);
        }
        if (argc > 3 && std::string(argv[1]) == "--dumpmpegaudio") {
            return dumpMpegAudio(argv[2], argv[3]);
        }
        // Unwraps a TAG0-style container (M1VC/PEBM), following nesting,
        // and reports what the payload turned out to be.
        if (argc > 2 && std::string(argv[1]) == "--tag0") {
            std::ifstream f(argv[2], std::ios::binary);
            if (!f) {
                std::fprintf(stderr, "could not open %s\n", argv[2]);
                return 1;
            }
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                        std::istreambuf_iterator<char>());
            auto payload = m2stream::extractTag0Data(bytes.data(), bytes.size());
            std::printf("container %zu bytes -> payload %zu bytes\n", bytes.size(),
                         payload.size());
            if (payload.size() >= 4) {
                std::printf("  first bytes: %02x %02x %02x %02x\n", payload[0], payload[1],
                             payload[2], payload[3]);
                bool mpeg = payload[0] == 0x00 && payload[1] == 0x00 && payload[2] == 0x01;
                std::printf("  looks like MPEG start code: %s\n", mpeg ? "YES" : "no");
            }
            if (argc > 3) {
                FILE* o = std::fopen(argv[3], "wb");
                if (o) {
                    std::fwrite(payload.data(), 1, payload.size(), o);
                    std::fclose(o);
                    std::printf("  wrote payload to %s\n", argv[3]);
                }
            }
            return 0;
        }
        if (argc > 4 && std::string(argv[1]) == "--dumpframe") {
            return dumpFrame(argv[2], size_t(std::atoi(argv[3])), argv[4]);
        }
        if (argc > 1 && argv[1][0] != '\0') testCinepakStream(argv[1]);
        if (argc > 2 && argv[2][0] != '\0') testMpegStream(argv[2]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "UNCAUGHT EXCEPTION: %s\n", e.what());
        return 1;
    }
    std::printf("libm2stream_tests: all checks passed\n");
    return 0;
}
