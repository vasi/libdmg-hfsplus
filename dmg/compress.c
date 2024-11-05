#include "dmg/dmg.h"
#include "dmg/compress.h"

#include <zlib.h>
#include <bzlib.h>
#include "dmg/adc.h"

#ifdef HAVE_LIBLZMA
  #include <lzma.h>

  static int decompressLZMA(unsigned char* inBuffer, size_t inSize, unsigned char* outBuffer, size_t outBufSize, size_t *decompSize) {
      lzma_ret lret;
      uint64_t memlimit = UINT64_MAX;
      size_t inPos = 0;
      *decompSize = 0;

      lret = lzma_stream_buffer_decode(&memlimit, LZMA_FAIL_FAST, NULL,
        inBuffer, &inPos, inSize, outBuffer, decompSize, outBufSize);
      return lret != LZMA_OK;
  }
#endif

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
#ifdef HAVE_LIBLZMA
  } else if (type == BLOCK_LZMA) {
    ret = decompressLZMA(inBuffer, inSize, outBuffer, outBufSize, &decompSize);
#endif
  } else {
    fprintf(stderr, "Unsupported block type: %#08x\n", type);
    return 1;
  }

  if (ret == 0) {
    ASSERT(decompSize == expectedSize, "Decompressed size mismatch");
  }
  return ret;
}
