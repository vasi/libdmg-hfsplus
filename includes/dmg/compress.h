#ifndef DMG_COMPRESS_H
#define DMG_COMPRESS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Return zero on success
int decompressRun(uint32_t type,
                  unsigned char* inBuffer, size_t inSize,
                  unsigned char* outBuffer, size_t outBufSize, size_t expectedSize);

#ifdef __cplusplus
}
#endif

#endif