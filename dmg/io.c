#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include <lzfse.h>
#include <pthread.h>

#include <dmg/dmg.h>
#include <dmg/adc.h>
#include <inttypes.h>

#define SECTORS_AT_A_TIME 0x800

typedef struct {
	uint32_t run;
	unsigned char* buf;
	size_t bufsize;
} block;

typedef struct {
	AbstractFile* out;
	AbstractFile* in;
	uint32_t numSectors;
	ChecksumFunc uncompressedChk;
	void* uncompressedChkToken;
	ChecksumFunc compressedChk;
	void* compressedChkToken;

	BLKXTable* blkx;

	uint32_t roomForRuns;
	uint32_t curRun;
	uint64_t curSector;
	size_t bufferSize;
} threadData;

void readBlock(threadData* d, block *inb) {
	if(d->curRun >= d->roomForRuns) {
		d->roomForRuns <<= 1;
		d->blkx = (BLKXTable*) realloc(d->blkx, sizeof(BLKXTable) + (d->roomForRuns * sizeof(BLKXRun)));
	}

	d->blkx->runs[d->curRun].type = BLOCK_LZFSE;
	d->blkx->runs[d->curRun].reserved = 0;
	d->blkx->runs[d->curRun].sectorStart = d->curSector;
	d->blkx->runs[d->curRun].sectorCount = (d->numSectors > SECTORS_AT_A_TIME) ? SECTORS_AT_A_TIME : d->numSectors;

	printf("run %d: sectors=%" PRId64 ", left=%d\n", d->curRun, d->blkx->runs[d->curRun].sectorCount, d->numSectors);

	inb->run = d->curRun;
	ASSERT((inb->bufsize = d->in->read(d->in, inb->buf, d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE)) == (d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE), "mRead");

	if(d->uncompressedChk)
		(*d->uncompressedChk)(d->uncompressedChkToken, inb->buf, d->blkx->runs[d->curRun].sectorCount * SECTOR_SIZE);

	d->blkx->runs[d->curRun].compLength = 0;

	d->curSector += d->blkx->runs[d->curRun].sectorCount;
	d->numSectors -= d->blkx->runs[d->curRun].sectorCount;
	d->curRun++;
}

void compressBlock(threadData* d, block *inb, block *outb) {
	outb->run = inb->run;
	outb->bufsize = lzfse_encode_buffer(outb->buf, d->bufferSize, inb->buf, inb->bufsize, NULL);
	ASSERT(outb->bufsize > 0, "compression error");
}

void* threadWorker(void* arg) {
	threadData* d;
	block inb;
	block outb;

	d = (threadData*)arg;
	ASSERT(inb.buf = (unsigned char*) malloc(d->bufferSize), "malloc");
	ASSERT(outb.buf = (unsigned char*) malloc(d->bufferSize), "malloc");

	while(d->numSectors > 0) {
		readBlock(d, &inb);
		compressBlock(d, &inb, &outb);

		d->blkx->runs[outb.run].compOffset = d->out->tell(d->out) - d->blkx->dataStart;
		if((outb.bufsize / SECTOR_SIZE) > d->blkx->runs[outb.run].sectorCount) {
			d->blkx->runs[outb.run].type = BLOCK_RAW;
			ASSERT(d->out->write(d->out, outb.buf, d->blkx->runs[outb.run].sectorCount * SECTOR_SIZE) == (d->blkx->runs[outb.run].sectorCount * SECTOR_SIZE), "fwrite");
			d->blkx->runs[outb.run].compLength += d->blkx->runs[outb.run].sectorCount * SECTOR_SIZE;

			if(d->compressedChk)
				(*d->compressedChk)(d->compressedChkToken, inb.buf, d->blkx->runs[outb.run].sectorCount * SECTOR_SIZE);

		} else {
			ASSERT(d->out->write(d->out, outb.buf, outb.bufsize) == outb.bufsize, "fwrite");

			if(d->compressedChk)
				(*d->compressedChk)(d->compressedChkToken, outb.buf, outb.bufsize);

			d->blkx->runs[outb.run].compLength += outb.bufsize;
		}
	}

	free(inb.buf);
	free(outb.buf);

	return NULL;
}

BLKXTable* insertBLKX(AbstractFile* out_, AbstractFile* in_, uint32_t firstSectorNumber, uint32_t numSectors_, uint32_t blocksDescriptor,
			uint32_t checksumType, ChecksumFunc uncompressedChk_, void* uncompressedChkToken_, ChecksumFunc compressedChk_,
			void* compressedChkToken_, Volume* volume, int zlibLevel) {
	threadData td;
	pthread_t thread;
	void* ret;

	td.out = out_;
	td.in = in_;
	td.numSectors = numSectors_;
	td.uncompressedChk = uncompressedChk_;
	td.uncompressedChkToken = uncompressedChkToken_;
	td.compressedChk = compressedChk_;
	td.compressedChkToken = compressedChkToken_;

	td.blkx = (BLKXTable*) malloc(sizeof(BLKXTable) + (2 * sizeof(BLKXRun)));
	td.roomForRuns = 2;
	memset(td.blkx, 0, sizeof(BLKXTable) + (td.roomForRuns * sizeof(BLKXRun)));

	td.blkx->fUDIFBlocksSignature = UDIF_BLOCK_SIGNATURE;
	td.blkx->infoVersion = 1;
	td.blkx->firstSectorNumber = firstSectorNumber;
	td.blkx->sectorCount = td.numSectors;
	td.blkx->dataStart = 0;
	td.blkx->decompressBufferRequested = SECTORS_AT_A_TIME + 8;
	td.blkx->blocksDescriptor = blocksDescriptor;
	td.blkx->reserved1 = 0;
	td.blkx->reserved2 = 0;
	td.blkx->reserved3 = 0;
	td.blkx->reserved4 = 0;
	td.blkx->reserved5 = 0;
	td.blkx->reserved6 = 0;
	memset(&(td.blkx->checksum), 0, sizeof(td.blkx->checksum));
	td.blkx->checksum.type = checksumType;
	td.blkx->checksum.size = 0x20;
	td.blkx->blocksRunCount = 0;

	td.bufferSize = SECTOR_SIZE * td.blkx->decompressBufferRequested;

	td.curRun = 0;
	td.curSector = 0;

	ASSERT(pthread_create(&thread, NULL, &threadWorker, &td) == 0, "thread create");
	ASSERT(pthread_join(thread, &ret) == 0, "thread join");
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
