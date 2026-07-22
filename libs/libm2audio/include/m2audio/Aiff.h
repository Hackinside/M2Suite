#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace m2audio {

// AIFF / AIFC audio file reader (EA-IFF-85 'FORM AIFF' / 'FORM AIFC' with
// COMM + SSND chunks — the public Apple/EA spec). The M2 SDK's audio
// sample library (3-1/tools/M2_3.1/Audio/SamplesV1.4, 580 files) is plain
// AIFF, which is why this is the lowest-risk of the MVP codecs.
//
// Scope: uncompressed PCM only. AIFC compression types other than 'NONE'
// (e.g. 'sowt' little-endian, 3DO 'SDX2'/ADPCM variants referenced by the
// SquashSnd tool) throw m2core::NotImplementedError until a real fixture
// motivates them.
class Aiff {
public:
    static Aiff load(const uint8_t* data, size_t size);
    static Aiff loadFromFile(const std::filesystem::path& path);

    uint16_t channels() const { return channels_; }
    uint32_t sampleFrames() const { return sampleFrames_; }
    uint16_t bitsPerSample() const { return bitsPerSample_; }
    double sampleRate() const { return sampleRate_; }
    bool isAifc() const { return isAifc_; }
    const std::string& compressionType() const { return compressionType_; }
    uint32_t compressionFourcc() const { return compressionFourcc_; }

    // Raw big-endian sample bytes from the SSND chunk (after its
    // offset/blockSize prologue).
    const std::vector<uint8_t>& rawSampleData() const { return sampleData_; }

    // Decodes to host-order interleaved 16-bit PCM. Handles uncompressed
    // 8/16-bit, and the 3DO 2:1 codecs SDX2 / SQS2 (squareroot family) and
    // CBD2 (cuberoot). Other compression types throw NotImplementedError.
    std::vector<int16_t> decodePcm16() const;

    // Serializes decodePcm16() output as a standard WAVE file (PCM16).
    std::vector<uint8_t> toWavBytes() const;

private:
    uint16_t channels_ = 0;
    uint32_t sampleFrames_ = 0;
    uint16_t bitsPerSample_ = 0;
    double sampleRate_ = 0.0;
    bool isAifc_ = false;
    std::string compressionType_ = "NONE";
    uint32_t compressionFourcc_ = 0x4E4F4E45; // 'NONE'
    std::vector<uint8_t> sampleData_;
};

} // namespace m2audio
