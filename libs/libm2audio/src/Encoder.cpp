#include "m2audio/Encoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace m2audio {

namespace {

// --- The delta-exact tables, identical to the decoder's -----------------
// Kept in sync with Sdx2.cpp by construction: same formulas, same
// clamping. An encoder that disagrees with its decoder by one LSB
// produces files that drift audibly over a long sample.
struct Tables {
    int16_t squares[256];
    int16_t cubes[256];
    Tables() {
        for (int i = -128; i < 128; ++i) {
            int32_t sq = i < 0 ? -(i * i) * 2 : (i * i) * 2;
            squares[uint8_t(i)] = int16_t(std::clamp(sq, -32768, 32767));
            int64_t v = int64_t(i) * 256;
            int64_t a = (v * v) >> 15;
            a = (a * v) >> 15;
            cubes[uint8_t(i)] = int16_t(std::clamp<int64_t>(a, -32768, 32767));
        }
    }
};
const Tables& tables() {
    static const Tables t;
    return t;
}

// Encodes one sample the way the decoder will read it back.
//
// Each byte is either "exact" (even: history is discarded, the table value
// IS the sample) or "delta" (odd: the table value is added to the running
// history). Both are searched and the closer one wins, which is what keeps
// the error from accumulating: a signal the delta table cannot reach in one
// step gets an exact byte instead of drifting toward it over many.
uint8_t encodeDeltaExactSample(int32_t target, int32_t& hist, const int16_t* table) {
    uint8_t best = 0;
    int32_t bestErr = INT32_MAX;
    int32_t bestValue = 0;

    for (int i = 0; i < 256; ++i) {
        uint8_t b = uint8_t(i);
        int32_t base = (b & 1) ? hist : 0; // odd = delta, even = exact
        int32_t value = std::clamp(base + table[b], -32768, 32767);
        int32_t err = std::abs(value - target);
        if (err < bestErr) {
            bestErr = err;
            best = b;
            bestValue = value;
            if (err == 0) {
                break;
            }
        }
    }
    hist = bestValue;
    return best;
}

// --- IMA/DVI ADPCM ------------------------------------------------------
constexpr int16_t kImaStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,
    23,    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,
    73,    80,    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,
    230,   253,   279,   307,   337,   371,   408,   449,   494,   544,   598,   658,
    724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
    7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};

constexpr int kImaIndexAdjust[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

// Standard IMA nibble search, mirroring the decoder's reconstruction so the
// predictor stays locked to what playback will compute.
uint8_t encodeImaNibble(int32_t sample, int32_t& predictor, int32_t& index) {
    int32_t step = kImaStep[index];
    int32_t diff = sample - predictor;
    uint8_t nibble = 0;
    if (diff < 0) {
        nibble = 8;
        diff = -diff;
    }
    int32_t delta = step >> 3;
    if (diff >= step) { nibble |= 4; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { nibble |= 2; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { nibble |= 1; delta += step; }

    predictor += (nibble & 8) ? -delta : delta;
    predictor = std::clamp(predictor, -32768, 32767);
    index = std::clamp(index + kImaIndexAdjust[nibble], 0, 88);
    return nibble;
}

// --- Big-endian writers (IFF is big-endian) -----------------------------
void put32(std::vector<uint8_t>& d, uint32_t v) {
    d.push_back(uint8_t(v >> 24));
    d.push_back(uint8_t(v >> 16));
    d.push_back(uint8_t(v >> 8));
    d.push_back(uint8_t(v));
}
void put16(std::vector<uint8_t>& d, uint16_t v) {
    d.push_back(uint8_t(v >> 8));
    d.push_back(uint8_t(v));
}
void putTag(std::vector<uint8_t>& d, const char* tag) {
    d.insert(d.end(), tag, tag + 4);
}

// AIFF stores the sample rate as an 80-bit IEEE 754 extended float. This is
// the one part of writing an AIFF that is easy to get subtly wrong, and a
// wrong rate drifts audibly over a long sample rather than failing.
void putExtended80(std::vector<uint8_t>& d, double value) {
    if (value <= 0.0) {
        d.insert(d.end(), 10, 0);
        return;
    }
    int exponent = 0;
    double mantissa = std::frexp(value, &exponent); // value = mantissa * 2^exp
    // frexp gives 0.5 <= mantissa < 1; the extended format wants an
    // explicit leading 1 bit, i.e. 1 <= m < 2.
    mantissa *= 2.0;
    exponent -= 1;
    uint16_t biasedExp = uint16_t(exponent + 16383);
    uint64_t fraction = uint64_t(std::llround(mantissa * double(1ULL << 63) / 2.0)) << 1;
    put16(d, biasedExp);
    for (int i = 7; i >= 0; --i) {
        d.push_back(uint8_t(fraction >> (i * 8)));
    }
}

void padToEven(std::vector<uint8_t>& d) {
    if (d.size() % 2) {
        d.push_back(0);
    }
}

} // namespace

EncodeOptions recommendedOptions() {
    EncodeOptions o;
    o.codec = AudioCodec::Sdx2;
    o.container = AudioContainer::Aifc;
    o.sampleRate = 22050;
    o.channels = 1;
    return o;
}

const char* codecName(AudioCodec codec) {
    switch (codec) {
        case AudioCodec::Pcm16: return "PCM 16-bit";
        case AudioCodec::Pcm8: return "PCM 8-bit";
        case AudioCodec::Sdx2: return "SDX2";
        case AudioCodec::Sqs2: return "SQS2";
        case AudioCodec::Cbd2: return "CBD2";
        case AudioCodec::Adp4: return "ADP4 (IMA ADPCM)";
    }
    return "?";
}

const char* codecDescription(AudioCodec codec) {
    switch (codec) {
        case AudioCodec::Pcm16:
            return "Uncompressed, exact. Twice the size of everything else — "
                   "use when quality matters more than disc space.";
        case AudioCodec::Pcm8:
            return "Uncompressed 8-bit. Same size as the 2:1 codecs but "
                   "noticeably noisier; SDX2 is the better trade.";
        case AudioCodec::Sdx2:
            return "Squareroot-delta-exact, 2:1. The 3DO workhorse — what "
                   "most shipping titles used, and what FILM/DataStreamer "
                   "audio expects. Recommended.";
        case AudioCodec::Sqs2:
            return "Same squareroot family as SDX2, tagged for the M2 DSP "
                   "FIFO's \"squash\" subtype. Choose it only if the target "
                   "engine asks for SQS2 by name.";
        case AudioCodec::Cbd2:
            return "Cuberoot-delta-exact, 2:1. Finer resolution near silence "
                   "than SDX2, coarser on loud material.";
        case AudioCodec::Adp4:
            return "4-bit IMA/DVI ADPCM, 4:1. The smallest option; good for "
                   "speech and effects, audibly rough on music.";
    }
    return "";
}

uint32_t codecFourcc(AudioCodec codec) {
    auto fourcc = [](const char* s) {
        return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16) |
               (uint32_t(uint8_t(s[2])) << 8) | uint32_t(uint8_t(s[3]));
    };
    switch (codec) {
        case AudioCodec::Pcm16:
        case AudioCodec::Pcm8: return fourcc("NONE");
        case AudioCodec::Sdx2: return fourcc("SDX2");
        case AudioCodec::Sqs2: return fourcc("SQS2");
        case AudioCodec::Cbd2: return fourcc("CBD2");
        case AudioCodec::Adp4: return fourcc("ADP4");
    }
    return fourcc("NONE");
}

uint32_t codecBitsPerSample(AudioCodec codec) {
    switch (codec) {
        case AudioCodec::Pcm16: return 16;
        case AudioCodec::Pcm8:
        case AudioCodec::Sdx2:
        case AudioCodec::Sqs2:
        case AudioCodec::Cbd2: return 8;
        case AudioCodec::Adp4: return 4;
    }
    return 16;
}

uint32_t bitrateBps(const EncodeOptions& options) {
    return options.sampleRate * options.channels * codecBitsPerSample(options.codec);
}

bool containerSupportsCodec(AudioContainer container, AudioCodec codec) {
    if (container == AudioContainer::Aifc) {
        return true; // AIFC carries a compression tag, so anything goes
    }
    return codec == AudioCodec::Pcm16 || codec == AudioCodec::Pcm8;
}

const std::vector<uint32_t>& standardSampleRates() {
    static const std::vector<uint32_t> rates{8000, 11025, 16000, 22050, 32000, 44100, 48000};
    return rates;
}

AudioBuffer resampleAndRemix(const AudioBuffer& in, uint32_t sampleRate, uint32_t channels) {
    AudioBuffer out;
    out.sampleRate = sampleRate ? sampleRate : in.sampleRate;
    out.channels = (channels == 1 || channels == 2) ? channels : 1;
    if (in.channels == 0 || in.samples.empty() || in.sampleRate == 0) {
        return out;
    }

    // Remix first, so resampling runs on the final channel count.
    const size_t inFrames = in.frames();
    std::vector<int16_t> mixed;
    mixed.resize(inFrames * out.channels);
    for (size_t f = 0; f < inFrames; ++f) {
        if (out.channels == 1) {
            int32_t acc = 0;
            for (uint32_t c = 0; c < in.channels; ++c) {
                acc += in.samples[f * in.channels + c];
            }
            mixed[f] = int16_t(std::clamp<int32_t>(acc / int32_t(in.channels), -32768, 32767));
        } else {
            for (uint32_t c = 0; c < 2; ++c) {
                // Mono source feeds both sides; a wider source takes its
                // first two channels.
                uint32_t src = (in.channels == 1) ? 0 : std::min(c, in.channels - 1);
                mixed[f * 2 + c] = in.samples[f * in.channels + src];
            }
        }
    }

    if (out.sampleRate == in.sampleRate) {
        out.samples = std::move(mixed);
        return out;
    }

    const double ratio = double(out.sampleRate) / double(in.sampleRate);
    const size_t outFrames = size_t(double(inFrames) * ratio);
    out.samples.resize(outFrames * out.channels);
    for (size_t f = 0; f < outFrames; ++f) {
        double srcPos = double(f) / ratio;
        size_t i0 = size_t(srcPos);
        size_t i1 = std::min(i0 + 1, inFrames - 1);
        double frac = srcPos - double(i0);
        for (uint32_t c = 0; c < out.channels; ++c) {
            double a = mixed[i0 * out.channels + c];
            double b = mixed[i1 * out.channels + c];
            out.samples[f * out.channels + c] =
                int16_t(std::clamp(a + (b - a) * frac, -32768.0, 32767.0));
        }
    }
    return out;
}

std::vector<uint8_t> encodeSamples(const AudioBuffer& in, AudioCodec codec) {
    std::vector<uint8_t> out;
    const uint32_t ch = in.channels ? in.channels : 1;

    switch (codec) {
        case AudioCodec::Pcm16: {
            out.reserve(in.samples.size() * 2);
            for (int16_t s : in.samples) {
                put16(out, uint16_t(s)); // AIFF is big-endian
            }
            break;
        }
        case AudioCodec::Pcm8: {
            out.reserve(in.samples.size());
            for (int16_t s : in.samples) {
                out.push_back(uint8_t(int8_t(s >> 8)));
            }
            break;
        }
        case AudioCodec::Sdx2:
        case AudioCodec::Sqs2:
        case AudioCodec::Cbd2: {
            const int16_t* table =
                (codec == AudioCodec::Cbd2) ? tables().cubes : tables().squares;
            out.reserve(in.samples.size());
            int32_t hist[8] = {};
            uint32_t c = 0;
            for (int16_t s : in.samples) {
                out.push_back(encodeDeltaExactSample(s, hist[c], table));
                c = (c + 1) % ch;
            }
            break;
        }
        case AudioCodec::Adp4: {
            // Per-channel predictor state, nibbles packed high-first to
            // match the decoder.
            out.reserve(in.samples.size() / 2 + 1);
            int32_t predictor[8] = {};
            int32_t index[8] = {};
            uint32_t c = 0;
            bool high = true;
            uint8_t pending = 0;
            for (int16_t s : in.samples) {
                uint8_t nib = encodeImaNibble(s, predictor[c], index[c]);
                if (high) {
                    pending = uint8_t(nib << 4);
                    high = false;
                } else {
                    out.push_back(uint8_t(pending | nib));
                    high = true;
                }
                c = (c + 1) % ch;
            }
            if (!high) {
                out.push_back(pending);
            }
            break;
        }
    }
    return out;
}

std::string validateOptions(const EncodeOptions& options) {
    if (options.channels != 1 && options.channels != 2) {
        return "Channel count must be 1 (mono) or 2 (stereo).";
    }
    if (options.sampleRate < 2000 || options.sampleRate > 48000) {
        return "Sample rate must be between 2000 and 48000 Hz.";
    }
    if (!containerSupportsCodec(options.container, options.codec)) {
        return std::string("AIFF cannot carry ") + codecName(options.codec) +
               " — it has no compression tag. Use AIFC.";
    }
    return {};
}

std::vector<uint8_t> encodeAudioFile(const AudioBuffer& in, const EncodeOptions& options) {
    AudioBuffer conv = resampleAndRemix(in, options.sampleRate, options.channels);
    std::vector<uint8_t> payload = encodeSamples(conv, options.codec);
    const uint32_t frames = uint32_t(conv.frames());
    const bool aifc = (options.container == AudioContainer::Aifc);

    // COMM
    std::vector<uint8_t> comm;
    put16(comm, uint16_t(conv.channels));
    put32(comm, frames);
    // sampleSize is the *decoded* width for compressed AIFC, and the
    // stored width for uncompressed — the 3DO codecs all decode to 16-bit.
    uint16_t sampleSize =
        (options.codec == AudioCodec::Pcm8) ? 8 : 16;
    put16(comm, sampleSize);
    putExtended80(comm, double(conv.sampleRate));
    if (aifc) {
        put32(comm, codecFourcc(options.codec));
        // Pascal string: length byte, text, padded to even.
        std::string name = codecName(options.codec);
        comm.push_back(uint8_t(name.size()));
        comm.insert(comm.end(), name.begin(), name.end());
        if (comm.size() % 2) {
            comm.push_back(0);
        }
    }

    // SSND: offset and blockSize, then the samples.
    std::vector<uint8_t> ssnd;
    put32(ssnd, 0); // offset
    put32(ssnd, 0); // blockSize
    ssnd.insert(ssnd.end(), payload.begin(), payload.end());
    padToEven(ssnd);

    std::vector<uint8_t> body;
    putTag(body, aifc ? "AIFC" : "AIFF");
    if (aifc) {
        // FVER is mandatory in AIFC and must come first.
        putTag(body, "FVER");
        put32(body, 4);
        put32(body, 0xA2805140); // the standard AIFC version timestamp
    }
    putTag(body, "COMM");
    put32(body, uint32_t(comm.size()));
    body.insert(body.end(), comm.begin(), comm.end());
    putTag(body, "SSND");
    put32(body, uint32_t(ssnd.size()));
    body.insert(body.end(), ssnd.begin(), ssnd.end());

    std::vector<uint8_t> file;
    putTag(file, "FORM");
    put32(file, uint32_t(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

} // namespace m2audio
