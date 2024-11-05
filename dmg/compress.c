#include "dmg/dmg.h"
#include "dmg/compress.h"

#include <zlib.h>
#include <bzlib.h>
#include "dmg/adc.h"

int decompressRun(uint32_t type,
                  unsigned char* inBuffer, size_t inSize,
                  unsigned char* outBuffer, size_t outBufSize, size_t expectedSize)
{
  size_t decompSize;
  int ret;

  if (type == BLOCK_ADC) {
    ret = (adc_decompress(inSize, inBuffer, outBufSize, outBuffer, &decompSize) != inSize);
  } else if (type == BLOCK_ZLIB) {
    decompSize = outBufSize;
    ret = (uncompress(outBuffer, &decompSize, inBuffer, inSize) != Z_OK);
  } else if (type == BLOCK_BZIP2) {
    unsigned int bz2DecompSize = outBufSize;
    ret = (BZ2_bzBuffToBuffDecompress(outBuffer, &bz2DecompSize, inBuffer, inSize, 0, 0) != BZ_OK);
    decompSize = bz2DecompSize;
  } else {
    fprintf(stderr, "Unsupported block type: %#08x\n", type);
    return 1;
  }

  if (ret == 0) {
    ASSERT(decompSize == expectedSize, "Decompressed size mismatch");
  }
  return ret;
}
