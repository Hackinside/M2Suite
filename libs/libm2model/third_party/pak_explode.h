#pragma once

// PKWARE DCL "implode" / explode decompressor used by Alone in the Dark's
// PAK archives (compressionType 1). Implementation is Mark Adler's public
// explode code, vendored from AITD-tools/Unpak (pak_explode.c). Not
// thread-safe (uses a static 32 KB window), which is fine for M2Suite's
// single-threaded asset loading.
//
// flags come from the PAK entry's CompressionFlags byte:
//   bit 2 (0x04) — a literal Huffman tree is present (min match length 3)
//   bit 1 (0x02) — 8 KB dictionary (else 4 KB)

#ifdef __cplusplus
extern "C" {
#endif

void PAK_explode(unsigned char* srcBuffer, unsigned char* dstBuffer,
                  unsigned int compressedSize, unsigned int uncompressedSize,
                  unsigned short flags);

#ifdef __cplusplus
}
#endif
