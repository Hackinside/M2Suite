// Round-trip tests for the 3DO audio encoders.
//
// The property that matters is that each encoder agrees with the decoder
// it will be played back through. A codec whose encoder and decoder differ
// by even one LSB per sample does not sound slightly wrong — the delta
// codecs carry history, so the error compounds and the file audibly drifts.
// So every test here encodes, decodes with the shipping decoder, and
// measures the error against the original.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "m2audio/Encoder.h"
#include "m2audio/Sdx2.h"

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, \
                          __LINE__);                                            \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

namespace {

// A signal with quiet passages, loud passages and sharp transients — the
// three things the delta codecs handle differently.
m2audio::AudioBuffer makeTestTone(uint32_t rate, uint32_t channels, size_t frames) {
    m2audio::AudioBuffer b;
    b.sampleRate = rate;
    b.channels = channels;
    b.samples.resize(frames * channels);
    for (size_t f = 0; f < frames; ++f) {
        double t = double(f) / double(rate);
        double env = 0.15 + 0.85 * std::fabs(std::sin(t * 2.0));
        double v = std::sin(t * 2.0 * 3.14159265 * 440.0) * env;
        if (f % 500 == 0) {
            v = (f % 1000 == 0) ? 0.98 : -0.98; // transients
        }
        for (uint32_t c = 0; c < channels; ++c) {
            double chv = (c == 0) ? v : v * 0.6;
            b.samples[f * channels + c] = int16_t(chv * 32000.0);
        }
    }
    return b;
}

double rmsError(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
    size_t n = std::min(a.size(), b.size());
    CHECK(n > 0);
    double acc = 0;
    for (size_t i = 0; i < n; ++i) {
        double d = double(a[i]) - double(b[i]);
        acc += d * d;
    }
    return std::sqrt(acc / double(n));
}

void testDeltaExactRoundTrip(m2audio::AudioCodec codec, const char* label, uint32_t channels,
                              double maxRms) {
    m2audio::AudioBuffer in = makeTestTone(22050, channels, 4000);
    std::vector<uint8_t> encoded = m2audio::encodeSamples(in, codec);
    // 2:1 — one byte per sample, whatever the channel count.
    CHECK(encoded.size() == in.samples.size());

    std::vector<int16_t> decoded =
        (codec == m2audio::AudioCodec::Cbd2)
            ? m2audio::decodeCbd2(encoded.data(), encoded.size(), channels)
            : m2audio::decodeSdx2(encoded.data(), encoded.size(), channels);
    CHECK(decoded.size() == in.samples.size());

    double err = rmsError(in.samples, decoded);
    std::printf("  %-6s %s: RMS error %.1f of 32000 full scale (%.2f%%)\n", label,
                 channels == 1 ? "mono  " : "stereo", err, 100.0 * err / 32000.0);
    CHECK(err < maxRms);
}

void testAdp4RoundTrip() {
    m2audio::AudioBuffer in = makeTestTone(22050, 1, 4000);
    std::vector<uint8_t> encoded = m2audio::encodeSamples(in, m2audio::AudioCodec::Adp4);
    CHECK(encoded.size() == in.samples.size() / 2); // 4:1

    std::vector<int16_t> decoded = m2audio::decodeAdp4(encoded.data(), encoded.size(), 1);
    CHECK(decoded.size() >= in.samples.size());
    decoded.resize(in.samples.size());

    double err = rmsError(in.samples, decoded);
    std::printf("  ADP4   mono  : RMS error %.1f of 32000 full scale (%.2f%%)\n", err,
                 100.0 * err / 32000.0);
    // 4-bit ADPCM on a signal with deliberate transients: looser bound than
    // the 2:1 codecs, but it must still track the waveform rather than
    // wander off it.
    CHECK(err < 3000.0);
}

void testPcmRoundTrip() {
    m2audio::AudioBuffer in = makeTestTone(22050, 2, 500);
    auto pcm16 = m2audio::encodeSamples(in, m2audio::AudioCodec::Pcm16);
    CHECK(pcm16.size() == in.samples.size() * 2);
    // Big-endian, as IFF requires.
    for (size_t i = 0; i < in.samples.size(); ++i) {
        int16_t v = int16_t((uint16_t(pcm16[i * 2]) << 8) | pcm16[i * 2 + 1]);
        CHECK(v == in.samples[i]);
    }
    auto pcm8 = m2audio::encodeSamples(in, m2audio::AudioCodec::Pcm8);
    CHECK(pcm8.size() == in.samples.size());
    std::printf("  PCM16/PCM8 sizes and byte order correct\n");
}

void testResampleAndRemix() {
    m2audio::AudioBuffer stereo = makeTestTone(44100, 2, 4410); // 100 ms
    m2audio::AudioBuffer mono = m2audio::resampleAndRemix(stereo, 22050, 1);
    CHECK(mono.channels == 1);
    CHECK(mono.sampleRate == 22050);
    // Half the rate, so about half the frames.
    CHECK(mono.frames() > 2100 && mono.frames() <= 2205);

    // Mono -> stereo duplicates rather than silencing one side.
    m2audio::AudioBuffer back = m2audio::resampleAndRemix(mono, 22050, 2);
    CHECK(back.channels == 2);
    CHECK(back.frames() == mono.frames());
    for (size_t f = 0; f < back.frames(); ++f) {
        CHECK(back.samples[f * 2] == back.samples[f * 2 + 1]);
    }
    std::printf("  resample 44100->22050 and stereo<->mono behave\n");
}

// Reads back the fields a 3DO player actually consumes.
void testAifcContainer() {
    m2audio::AudioBuffer in = makeTestTone(44100, 2, 2000);
    m2audio::EncodeOptions opt = m2audio::recommendedOptions();
    CHECK(opt.codec == m2audio::AudioCodec::Sdx2);
    CHECK(opt.sampleRate == 22050);
    CHECK(opt.channels == 1);

    std::vector<uint8_t> file = m2audio::encodeAudioFile(in, opt);
    CHECK(file.size() > 64);
    CHECK(std::memcmp(file.data(), "FORM", 4) == 0);
    CHECK(std::memcmp(file.data() + 8, "AIFC", 4) == 0);

    auto find = [&](const char* tag) -> size_t {
        for (size_t i = 12; i + 4 <= file.size(); ++i) {
            if (std::memcmp(file.data() + i, tag, 4) == 0) {
                return i;
            }
        }
        return SIZE_MAX;
    };
    size_t fver = find("FVER");
    size_t comm = find("COMM");
    size_t ssnd = find("SSND");
    CHECK(fver != SIZE_MAX && comm != SIZE_MAX && ssnd != SIZE_MAX);
    CHECK(fver < comm); // FVER must come first in an AIFC
    CHECK(comm < ssnd);

    auto rd16 = [&](size_t o) { return uint16_t((uint16_t(file[o]) << 8) | file[o + 1]); };
    auto rd32 = [&](size_t o) {
        return (uint32_t(file[o]) << 24) | (uint32_t(file[o + 1]) << 16) |
               (uint32_t(file[o + 2]) << 8) | uint32_t(file[o + 3]);
    };
    size_t c = comm + 8;
    CHECK(rd16(c) == 1);         // channels, after the remix to mono
    uint32_t frames = rd32(c + 2);
    CHECK(rd16(c + 6) == 16);    // SDX2 decodes to 16-bit
    // 80-bit extended sample rate: exponent then an explicit-1 mantissa.
    uint16_t biasedExp = rd16(c + 8);
    CHECK(biasedExp == 16383 + 14); // 22050 lies in [2^14, 2^15)
    CHECK(std::memcmp(file.data() + c + 18, "SDX2", 4) == 0);

    // 22050/44100 of 2000 frames = 1000, and SDX2 is one byte per sample.
    CHECK(frames > 950 && frames <= 1000);
    uint32_t ssndSize = rd32(ssnd + 4);
    CHECK(ssndSize >= frames + 8);
    std::printf("  AIFC container: FVER/COMM/SSND ordered, rate and tag correct\n");
}

void testBitrateAndValidation() {
    m2audio::EncodeOptions o = m2audio::recommendedOptions();
    CHECK(m2audio::bitrateBps(o) == 22050 * 1 * 8); // SDX2 mono = 176400 bps
    o.codec = m2audio::AudioCodec::Pcm16;
    CHECK(m2audio::bitrateBps(o) == 22050 * 1 * 16);
    o.channels = 2;
    CHECK(m2audio::bitrateBps(o) == 22050 * 2 * 16);
    o.codec = m2audio::AudioCodec::Adp4;
    CHECK(m2audio::bitrateBps(o) == 22050 * 2 * 4);

    // AIFF has no compression tag, so it must reject the codecs.
    m2audio::EncodeOptions bad = m2audio::recommendedOptions();
    bad.container = m2audio::AudioContainer::Aiff;
    CHECK(!m2audio::validateOptions(bad).empty());
    bad.codec = m2audio::AudioCodec::Pcm16;
    CHECK(m2audio::validateOptions(bad).empty());

    m2audio::EncodeOptions badCh = m2audio::recommendedOptions();
    badCh.channels = 5;
    CHECK(!m2audio::validateOptions(badCh).empty());
    std::printf("  bitrate arithmetic and option validation correct\n");
}

} // namespace

int main() {
    std::printf("test_encoder:\n");
    testDeltaExactRoundTrip(m2audio::AudioCodec::Sdx2, "SDX2", 1, 900.0);
    testDeltaExactRoundTrip(m2audio::AudioCodec::Sdx2, "SDX2", 2, 900.0);
    testDeltaExactRoundTrip(m2audio::AudioCodec::Cbd2, "CBD2", 1, 1500.0);
    testAdp4RoundTrip();
    testPcmRoundTrip();
    testResampleAndRemix();
    testAifcContainer();
    testBitrateAndValidation();
    std::printf("all encoder tests passed\n");
    return 0;
}
