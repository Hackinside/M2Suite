#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace m2audio {

// Encoding audio *into* the 3DO M1/M2 formats, for asset replacement and
// translation work. The decoders live in Sdx2.h / Aiff.h; this is the
// other direction.
//
// Every 3DO codec here is a 2:1 or 4:1 fixed-ratio scheme with no
// configurable quality, so "bitrate" is not a free parameter — it falls
// out of sample rate x channels x bits per sample. bitrateBps() reports
// the result rather than pretending it can be dialled.

enum class AudioCodec {
    Pcm16,  // uncompressed, 16-bit big-endian. Largest, exact.
    Pcm8,   // uncompressed, 8-bit signed. Halves size, audibly noisy.
    Sdx2,   // squareroot-delta-exact 2:1 — the 3DO workhorse
    Sqs2,   // same family, tagged for the M2 DSP FIFO's "squash" subtype
    Cbd2,   // cuberoot-delta-exact 2:1
    Adp4,   // 4-bit IMA/DVI ADPCM, 4:1
};

// The container to wrap the encoded samples in.
enum class AudioContainer {
    Aiff, // 'FORM'+'AIFF' — uncompressed only
    Aifc, // 'FORM'+'AIFC' — carries a compression tag; needed for the codecs
};

struct AudioBuffer {
    std::vector<int16_t> samples; // interleaved, native endian
    uint32_t sampleRate = 22050;
    uint32_t channels = 1;
    size_t frames() const { return channels ? samples.size() / channels : 0; }
};

struct EncodeOptions {
    AudioCodec codec = AudioCodec::Sdx2;
    AudioContainer container = AudioContainer::Aifc;
    uint32_t sampleRate = 22050;
    uint32_t channels = 1;
};

// The defaults that match what shipping 3DO titles actually used: SDX2 at
// 22050 Hz mono in an AIFC. Good quality per byte, decodes on both M1 and
// M2, and it is what the FILM/DataStreamer audio path expects.
EncodeOptions recommendedOptions();

// Human-readable names, for building UI without duplicating the tables.
const char* codecName(AudioCodec codec);
const char* codecDescription(AudioCodec codec);
// FOURCC as stored in an AIFC 'COMM' chunk ('NONE', 'SDX2', ...).
uint32_t codecFourcc(AudioCodec codec);
// Bits per encoded sample: 16, 8 or 4.
uint32_t codecBitsPerSample(AudioCodec codec);
// Encoded bits per second at these settings. Fixed-ratio codecs, so this
// is exact rather than an estimate.
uint32_t bitrateBps(const EncodeOptions& options);
// True when the container can carry the codec: AIFF has no compression
// tag, so anything other than PCM needs AIFC.
bool containerSupportsCodec(AudioContainer container, AudioCodec codec);

// Sample rates the 3DO hardware is documented for. Others work, but the
// DSP resamples them at playback, which costs quality and cycles.
const std::vector<uint32_t>& standardSampleRates();

// Resamples and/or remixes to the target rate and channel count. Linear
// interpolation: the source material here is already low-rate 8/16-bit
// game audio, and a windowed-sinc pass would be inaudible against the
// quantisation noise the codecs add next.
AudioBuffer resampleAndRemix(const AudioBuffer& in, uint32_t sampleRate, uint32_t channels);

// Encodes to raw codec bytes, without a container.
std::vector<uint8_t> encodeSamples(const AudioBuffer& in, AudioCodec codec);

// Encodes and wraps in a complete AIFF/AIFC file. Applies the rate and
// channel conversion first, so `in` may be any rate.
std::vector<uint8_t> encodeAudioFile(const AudioBuffer& in, const EncodeOptions& options);

// Empty when the options are usable; otherwise why not.
std::string validateOptions(const EncodeOptions& options);

} // namespace m2audio
