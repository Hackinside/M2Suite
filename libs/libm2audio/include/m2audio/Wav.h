#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace m2audio {

// Minimal RIFF/WAVE reader (PCM 8/16-bit) — some M2-era disc content
// (e.g. Oldsmobile) ships standard WAV files alongside 3DO formats, and
// the waveform view consumes the same PCM16 shape as Aiff::decodePcm16.
class Wav {
public:
    static Wav load(const uint8_t* data, size_t size);
    static Wav loadFromFile(const std::filesystem::path& path);

    uint16_t channels() const { return channels_; }
    uint32_t sampleRate() const { return sampleRate_; }
    uint16_t bitsPerSample() const { return bitsPerSample_; }

    // Host-order interleaved 16-bit PCM (8-bit unsigned widened).
    std::vector<int16_t> decodePcm16() const;

private:
    uint16_t channels_ = 0;
    uint32_t sampleRate_ = 0;
    uint16_t bitsPerSample_ = 0;
    std::vector<uint8_t> data_;
};

} // namespace m2audio
