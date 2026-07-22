#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace m2audio {

// 3DO "delta-exact" 2:1 audio codecs (reference: vgmstream
// sdx2_decoder.c and the M2 audio folio DSP source). Each byte encodes one
// sample via a 256-entry expansion table; even bytes are absolute values,
// odd bytes are deltas from the previous sample of the same channel.
//
//   SDX2 — squareroot-delta-exact: table[i] = sign(i) * 2 * i^2
//   SQS2 — the M2 DSP hardware FIFO's "squash" subtype; same squareroot
//          family as SDX2 (DRSC_INFIFO_SUBTYPE_SQS2 in the audio folio)
//   CBD2 — cuberoot-delta-exact: table[i] = (i^3) / 64 with the DSP's
//          arithmetic-shift (floor) rounding
//
// Samples are channel-interleaved; each channel keeps its own history.
std::vector<int16_t> decodeSdx2(const uint8_t* data, size_t size, uint32_t channels);
std::vector<int16_t> decodeCbd2(const uint8_t* data, size_t size, uint32_t channels);

// ADP4 — the AIFC compression the 3DO audio folio documents as "4-bit
// Intel/DVI format" (sub_decode_adp4.ins), i.e. standard IMA ADPCM: a
// 4-bit nibble per sample driving an 89-entry step table (starting at 7)
// plus a step-index adjustment table. Nibbles are high-first within each
// byte. Used by Yu Yu Hakusho's SOUND/*.sc effects.
std::vector<int16_t> decodeAdp4(const uint8_t* data, size_t size, uint32_t channels);

} // namespace m2audio
