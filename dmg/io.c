#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include <bzlib.h>
#include <pthread.h>

#include <dmg/dmg.h>
#include <dmg/adc.h>
#include <dmg/attribution.h>
#include <inttypes.h>

// Okay, this value sucks. You shouldn't touch it because it affects how many ignore sections get added to the blkx list
// If the blkx list gets too fragmented with ignore sections, then the copy list in certain versions of the iPhone's
// asr becomes too big. Due to Apple's BUGGY CODE, this causes asr to segfault! This is because the copy list becomes
// too large for the initial buffer allocated, and realloc is called by asr. Unfortunately, after the realloc, the initial
// pointer is still used by asr for a little while! Frakking noob mistake.

// The only reason why it works at all is their really idiotic algorithm to determine where to put ignore blocks. It's
// certainly nothing reasonable like "put in an ignore block if you encounter more than X blank sectors" (like mine)
// There's always a large-ish one at the end, and a tiny 2 sector one at the end too, to take care of the space after
// the backup volume header. No frakking clue how they go about determining how to do that.

#define SECTORS_AT_A_TIME 0x200

typedef struct {
	uint32_t run;

	unsigned char* inbuf;
	size_t insize;

	unsigned char* outbuf;
	size_t outsize;
} block;

typedef struct {
	AbstractFile* out;
	AbstractFile* in;
	uint32_t numSectors;
	ChecksumFunc uncompressedChk;
	void* uncompressedChkToken;
	ChecksumFunc compressedChk;
	void* compressedChkToken;
	AbstractAttribution* attribution;

	BLKXTable *blkx;
	uint32_t roomForRuns;
	uint32_t curRun;
	uint64_t curSector;
	size_t bufferSize;
	int IGNORE_THRESHOLD;
	uint64_t startOff;
	enum ShouldKeepRaw keepRaw;
} threadData;

void *threadWorker(void* arg) {
	threadData* d = (threadData*)arg;
	block b;
	unsigned char *nextInBuffer;

	ASSERT(b.inbuf = (unsigned char*)malloc(d->bufferSize), "malloc");
	ASSERT(b.outbuf = (unsigned char*)malloc(d->bufferSize), "malloc");
	ASSERT(nextInBuffer = (unsigned char*) malloc(d->bufferSize), "malloc");

	while(d->numSectors > 0) {
		int ret;
		bz_stream strm;

		if(d->curRun >= d->roomForRuns) {
			d->roomForRuns <<= 1;
			d->blkx = (BLKXTable*) realloc(d->blkx, sizeof(BLKXTable) + (d->roomForRuns * sizeof(BLKXRun)));
		}
		d->blkx->runs[d->curRun].type = BLOCK_BZIP2;
		d->blkx->runs[d->curRun].reserved = 0;
		d->blkx->runs[d->curRun].sectorStart = d->curSector;
		d->blkx->runs[d->curRun].sectorCount = (d->numSectors > SECTORS_AT_A_TIME) ? SECTORS_AT_A_TIME : d->numSectors;

		memset(&strm, 0, sizeof(strm));
		strm.bzalloc = Z_NULL;
		strm.bzfree = Z_NULL;
		strm.opaque = Z_NULL;

		int nextAmountRead = 0;
		d->blkx->runs[d->curRun].sectorCount = (d->numSectors > SECTORS_AT_A_TIME) ? SECTORS_AT_A_TIME : d->numSectors;

		//printf("Currently at %" PRId64 "\n", curOff);
		off_t sectorStart = d->startOff + (d->blkx->sectorCount - d->numSectors) * SECTOR_SIZE;
		d->in->seek(d->in, sectorStart);
		b.run = d->curRun;
		ASSERT((b.insize = d->in->read(d->in, b.inbuf, d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE)) == (d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE), "mRead");

		if (d->numSectors - d->blkx->runs[d->curRun].sectorCount > 0) {
			// No need to rewind `inBuffer` because the next iteration of the loop
			// calls `seek` anyways.
			nextAmountRead = d->in->read(d->in, nextInBuffer, d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE);
		}

		// printf("run %d: sectors=%" PRId64 ", left=%d\n", d->curRun, d->blkx->runs[d->curRun].sectorCount, d->numSectors);

		ASSERT(BZ2_bzCompressInit(&strm, 9, 0, 0) == BZ_OK, "BZ2_bzCompressInit");

		strm.avail_in = b.insize;
		strm.next_in = (char*)b.inbuf;

		if (d->attribution) {
			// We either haven't found the sentinel value yet, or are already past it.
			// Either way, keep searching.
			if (d->keepRaw == KeepNoneRaw) {
				d->keepRaw = d->attribution->shouldKeepRaw(d->attribution, b.inbuf, b.insize, nextInBuffer, nextAmountRead);
			}
			// KeepCurrentAndNextRaw means that the *previous* time through the loop `shouldKeepRaw`
			// found the sentinel string, and that it crosses two runs. The previous
			// loop already kept its run raw, and so must we. We don't want the _next_ run
			// to also be raw though, so we adjust this appropriately.
			// Note that KeepCurrentRaw will switch to KeepNoneRaw further down, when we've
			// set the run raw.
			else if (d->keepRaw == KeepCurrentAndNextRaw) {
				d->keepRaw = KeepCurrentRaw;
			}
			else if (d->keepRaw == KeepCurrentRaw) {
				d->keepRaw = KeepRemainingRaw;
			}
			// printf("keepRaw = %d (%p, %ld)\n", d->keepRaw, b.inbuf, b.insize);
		}

		if(d->uncompressedChk)
			(*d->uncompressedChk)(d->uncompressedChkToken, b.inbuf, d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE);

		d->blkx->runs[d->curRun].compOffset = d->out->tell(d->out) - d->blkx->dataStart;
		d->blkx->runs[d->curRun].compLength = 0;

		strm.avail_out = d->bufferSize;
		strm.next_out = (char*)b.outbuf;

		ASSERT((ret = BZ2_bzCompress(&strm, BZ_FINISH)) != BZ_SEQUENCE_ERROR, "BZ2_bzCompress/BZ_SEQUENCE_ERROR");
		if(ret != BZ_STREAM_END) {
			ASSERT(FALSE, "BZ2_bzCompress");
		}
		b.outsize = d->bufferSize - strm.avail_out;

		if(d->keepRaw == KeepCurrentRaw || d->keepRaw == KeepCurrentAndNextRaw || ((b.outsize / SECTOR_SIZE) >= (d->blkx->runs[d->curRun].sectorCount - 15))) {
			printf("Setting type = BLOCK_RAW\n");
			d->blkx->runs[d->curRun].type = BLOCK_RAW;
			ASSERT(d->out->write(d->out, b.inbuf, d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE) == (d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE), "fwrite");
			d->blkx->runs[d->curRun].compLength += d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE;


			if(d->compressedChk)
				(*d->compressedChk)(d->compressedChkToken, b.inbuf, d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE);

			if (d->attribution) {
				// In a raw block, uncompressed and compressed data is identical.
				d->attribution->observeBuffers(d->attribution, d->keepRaw,
											b.inbuf, d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE,
											b.inbuf, d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE);
			}
		} else {
			ASSERT(d->out->write(d->out, b.outbuf, b.outsize) == b.outsize, "fwrite");

			if(d->compressedChk)
				(*d->compressedChk)(d->compressedChkToken, b.outbuf, b.outsize);

			if (d->attribution) {
				// In a bzip2 block, uncompressed and compressed data are not the same.
				d->attribution->observeBuffers(d->attribution, d->keepRaw,
											b.inbuf, b.insize,
											b.outbuf, b.outsize);
			}

			d->blkx->runs[d->curRun].compLength += b.outsize;
		}

		BZ2_bzCompressEnd(&strm);

		d->curSector += d->blkx->runs[d->curRun].sectorCount;
		d->numSectors -= d->blkx->runs[d->curRun].sectorCount;
		d->curRun++;
	}

	free(b.inbuf);
	free(b.outbuf);
}

BLKXTable* insertBLKX(AbstractFile* out_, AbstractFile* in_, uint32_t firstSectorNumber, uint32_t numSectors_, uint32_t blocksDescriptor,
			uint32_t checksumType, ChecksumFunc uncompressedChk_, void* uncompressedChkToken_, ChecksumFunc compressedChk_,
			void* compressedChkToken_, Volume* volume, AbstractAttribution* attribution_) {
	threadData td = {
		.out = out_,
		.in = in_,
		.numSectors = numSectors_,
		.uncompressedChk = uncompressedChk_,
		.uncompressedChkToken = uncompressedChkToken_,
		.compressedChk = compressedChk_,
		.compressedChkToken = compressedChkToken_,
		.attribution = attribution_,
		.IGNORE_THRESHOLD = 100000,
	};

	td.blkx = (BLKXTable*) malloc(sizeof(BLKXTable) + (2 * sizeof(BLKXRun)));
	td.roomForRuns = 2;
	memset(td.blkx, 0, sizeof(BLKXTable) + (td.roomForRuns * sizeof(BLKXRun)));

	td.blkx->fUDIFBlocksSignature = UDIF_BLOCK_SIGNATURE;
	td.blkx->infoVersion = 1;
	td.blkx->firstSectorNumber = firstSectorNumber;
	td.blkx->sectorCount = td.numSectors;
	td.blkx->dataStart = 0;
	td.blkx->decompressBufferRequested = 0x208;
	td.blkx->blocksDescriptor = blocksDescriptor;
	td.blkx->reserved1 = 0;
	td.blkx->reserved2 = 0;
	td.blkx->reserved3 = 0;
	td.blkx->reserved4 = 0;
	td.blkx->reserved5 = 0;
	td.blkx->reserved6 = 0;
	memset(&(td.blkx->checksum), 0, sizeof(td.blkx->checksum));
	td.blkx->checksum.type = checksumType;
	td.blkx->checksum.bitness = checksumBitness(checksumType);
	td.blkx->blocksRunCount = 0;

	td.bufferSize = SECTOR_SIZE * td.blkx->decompressBufferRequested;

	td.curRun = 0;
	td.curSector = 0;

	td.startOff = td.in->tell(td.in);
	td.keepRaw = KeepNoneRaw;

	pthread_t thread;
	ASSERT(pthread_create(&thread, NULL, threadWorker, &td) == 0, "pthread_create");
	void *ret;
	ASSERT(pthread_join(thread, &ret) == 0, "pthread_join");
	ASSERT(ret == NULL, "thread return");

	if(td.curRun >= td.roomForRuns) {
		td.roomForRuns <<= 1;
		td.blkx = (BLKXTable*) realloc(td.blkx, sizeof(BLKXTable) + (td.roomForRuns * sizeof(BLKXRun)));
	}

	td.blkx->runs[td.curRun].type = BLOCK_TERMINATOR;
	td.blkx->runs[td.curRun].reserved = 0;
	td.blkx->runs[td.curRun].sectorStart = td.curSector;
	td.blkx->runs[td.curRun].sectorCount = 0;
	td.blkx->runs[td.curRun].compOffset = td.out->tell(td.out) - td.blkx->dataStart;
	td.blkx->runs[td.curRun].compLength = 0;
	td.blkx->blocksRunCount = td.curRun + 1;

	return td.blkx;
}

#define DEFAULT_BUFFER_SIZE (1 * 1024 * 1024)

void extractBLKX(AbstractFile* in, AbstractFile* out, BLKXTable* blkx) {
	unsigned char* inBuffer;
	unsigned char* outBuffer;
	unsigned char zero;
	size_t bufferSize;
	size_t have;
	off_t initialOffset;
	int i;
	int ret;

	z_stream strm;
	bz_stream bzstrm;

	bufferSize = SECTOR_SIZE * blkx->decompressBufferRequested;

	ASSERT(inBuffer = (unsigned char*) malloc(bufferSize), "malloc");
	ASSERT(outBuffer = (unsigned char*) malloc(bufferSize), "malloc");

	initialOffset =	out->tell(out);
	ASSERT(initialOffset != -1, "ftello");

	zero = 0;

	for(i = 0; i < blkx->blocksRunCount; i++) {
		ASSERT(in->seek(in, blkx->dataStart + blkx->runs[i].compOffset) == 0, "fseeko");
		ASSERT(out->seek(out, initialOffset + (blkx->runs[i].sectorStart * SECTOR_SIZE)) == 0, "mSeek");

		if(blkx->runs[i].sectorCount > 0) {
			ASSERT(out->seek(out, initialOffset + (blkx->runs[i].sectorStart + blkx->runs[i].sectorCount) * SECTOR_SIZE - 1) == 0, "mSeek");
			ASSERT(out->write(out, &zero, 1) == 1, "mWrite");
			ASSERT(out->seek(out, initialOffset + (blkx->runs[i].sectorStart * SECTOR_SIZE)) == 0, "mSeek");
		}

		if(blkx->runs[i].type == BLOCK_TERMINATOR) {
			break;
		}

		if( blkx->runs[i].compLength == 0) {
			continue;
		}

		printf("run %d: start=%" PRId64 " sectors=%" PRId64 ", length=%" PRId64 ", fileOffset=0x%" PRIx64 "\n", i, initialOffset + (blkx->runs[i].sectorStart * SECTOR_SIZE), blkx->runs[i].sectorCount, blkx->runs[i].compLength, blkx->runs[i].compOffset);

		switch(blkx->runs[i].type) {
			case BLOCK_ADC:
                        {
                                size_t bufferRead = 0;
				do {
					ASSERT((strm.avail_in = in->read(in, inBuffer, blkx->runs[i].compLength)) == blkx->runs[i].compLength, "fread");
					strm.avail_out = adc_decompress(strm.avail_in, inBuffer, bufferSize, outBuffer, &have);
					ASSERT(out->write(out, outBuffer, have) == have, "mWrite");
					bufferRead+=strm.avail_out;
				} while (bufferRead < blkx->runs[i].compLength);
				break;
                        }

			case BLOCK_ZLIB:
				strm.zalloc = Z_NULL;
				strm.zfree = Z_NULL;
				strm.opaque = Z_NULL;
				strm.avail_in = 0;
				strm.next_in = Z_NULL;

				ASSERT(inflateInit(&strm) == Z_OK, "inflateInit");

				ASSERT((strm.avail_in = in->read(in, inBuffer, blkx->runs[i].compLength)) == blkx->runs[i].compLength, "fread");
				strm.next_in = inBuffer;

				do {
					strm.avail_out = bufferSize;
					strm.next_out = outBuffer;
					ASSERT((ret = inflate(&strm, Z_NO_FLUSH)) != Z_STREAM_ERROR, "inflate/Z_STREAM_ERROR");
					if(ret != Z_OK && ret != Z_BUF_ERROR && ret != Z_STREAM_END) {
						ASSERT(FALSE, "inflate");
					}
					have = bufferSize - strm.avail_out;
					ASSERT(out->write(out, outBuffer, have) == have, "mWrite");
				} while (strm.avail_out == 0);

				ASSERT(inflateEnd(&strm) == Z_OK, "inflateEnd");
				break;
			case BLOCK_RAW:
				if(blkx->runs[i].compLength > bufferSize) {
					uint64_t left = blkx->runs[i].compLength;
					void* pageBuffer = malloc(DEFAULT_BUFFER_SIZE);
					while(left > 0) {
						size_t thisRead;
						if(left > DEFAULT_BUFFER_SIZE) {
							thisRead = DEFAULT_BUFFER_SIZE;
						} else {
							thisRead = left;
						}
						ASSERT((have = in->read(in, pageBuffer, thisRead)) == thisRead, "fread");
						ASSERT(out->write(out, pageBuffer, have) == have, "mWrite");
						left -= have;
					}
					free(pageBuffer);
				} else {
					ASSERT((have = in->read(in, inBuffer, blkx->runs[i].compLength)) == blkx->runs[i].compLength, "fread");
					ASSERT(out->write(out, inBuffer, have) == have, "mWrite");
				}
				break;
			case BLOCK_IGNORE:
				break;
			case BLOCK_BZIP2:
				bzstrm.bzalloc = Z_NULL;
				bzstrm.bzfree = Z_NULL;
				bzstrm.opaque = Z_NULL;
				bzstrm.avail_in = 0;
				bzstrm.next_in = Z_NULL;
				ASSERT(BZ2_bzDecompressInit(&bzstrm, 0, 0) == BZ_OK, "BZ2_bzDecompressInit");
				ASSERT((bzstrm.avail_in = in->read(in, inBuffer, blkx->runs[i].compLength)) == blkx->runs[i].compLength, "fread");
				bzstrm.next_in = (char*)inBuffer;
				do {
					bzstrm.avail_out = bufferSize;
					bzstrm.next_out = (char*)outBuffer;
					ASSERT((ret = BZ2_bzDecompress(&bzstrm)) >= 0, "BZ2_bzDecompress");
					have = bufferSize - bzstrm.avail_out;
					ASSERT(out->write(out, outBuffer, have) == have, "mWrite");
				} while (bzstrm.avail_out == 0);
				ASSERT(BZ2_bzDecompressEnd(&bzstrm) == BZ_OK, "BZ2_bzDecompressEnd");
				break;
			case BLOCK_COMMENT:
				break;
			case BLOCK_TERMINATOR:
				break;
			default:
				break;
		}
	}

	free(inBuffer);
	free(outBuffer);
}
