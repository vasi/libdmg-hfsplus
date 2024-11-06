#ifndef DMG_COMPRESS_H
#define DMG_COMPRESS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Return zero on success
typedef int (*CompressFunc)(unsigned char* inBuffer, size_t inSize,
                            unsigned char* outBuffer, size_t outBufSize, size_t *compSize);

typedef struct {
  uint32_t block_type;
  CompressFunc compress;
} Compressor;

// Pass NULL name to get the default. Asserts on failure.
const void getCompressor(Compressor *comp, char *name);

// Return zero on success
int decompressRun(uint32_t type,
                  unsigned char* inBuffer, size_t inSize,
                  unsigned char* outBuffer, size_t outBufSize, size_t expectedSize);

#ifdef __cplusplus
}
#endif

#endif