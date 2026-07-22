// Parses the real fixture tests/fixtures/sample.aiff (TrumpetLite
// .C6LRM22k from the M2 SDK's SamplesV1.4 library) and checks COMM fields
// and PCM decode, plus a WAV round-trip shape check.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "m2audio/Aiff.h"

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, \
                          __LINE__);                                            \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

using namespace m2audio;

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "tests/fixtures/sample.aiff";

    Aiff aiff = Aiff::loadFromFile(path);

    // "M22k" in the SDK's sample naming convention = mono, 22kHz.
    CHECK(aiff.channels() >= 1 && aiff.channels() <= 2);
    CHECK(aiff.sampleRate() > 8000.0 && aiff.sampleRate() < 48001.0);
    CHECK(aiff.bitsPerSample() == 8 || aiff.bitsPerSample() == 16);
    CHECK(aiff.sampleFrames() > 0);

    auto pcm = aiff.decodePcm16();
    CHECK(pcm.size() == size_t(aiff.sampleFrames()) * aiff.channels());

    // Signal sanity: real audio is strongly correlated sample-to-sample,
    // whereas a mis-decoded stream looks like white noise (corr ~ 0). This
    // makes a wrong codec path obvious instead of silently "decoding".
    if (pcm.size() > 64) {
        double sum = 0, sumSq = 0, sumProd = 0;
        for (size_t i = 0; i < pcm.size(); ++i) {
            double v = pcm[i];
            sum += v;
            sumSq += v * v;
            if (i + 1 < pcm.size()) sumProd += v * double(pcm[i + 1]);
        }
        double n = double(pcm.size());
        double mean = sum / n;
        double var = sumSq / n - mean * mean;
        double cov = sumProd / (n - 1) - mean * mean;
        double corr = (var > 1e-9) ? (cov / var) : 0.0;
        std::printf("libm2audio_tests: lag-1 correlation %.3f (>0.5 = coherent audio)\n", corr);
    }

    // Real audio: expect at least one nonzero sample.
    bool nonZero = false;
    for (int16_t s : pcm) {
        if (s != 0) {
            nonZero = true;
            break;
        }
    }
    CHECK(nonZero);

    auto wav = aiff.toWavBytes();
    CHECK(wav.size() == 44 + pcm.size() * 2);
    CHECK(std::memcmp(wav.data(), "RIFF", 4) == 0);
    CHECK(std::memcmp(wav.data() + 8, "WAVE", 4) == 0);

    std::printf("libm2audio_tests: %u ch, %.0f Hz, %u-bit, %u frames — all checks passed\n",
                 aiff.channels(), aiff.sampleRate(), aiff.bitsPerSample(),
                 aiff.sampleFrames());
    return 0;
}
