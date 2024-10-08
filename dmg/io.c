#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include <lzfse.h>
#include <pthread.h>
#include <unistd.h>

#include <dmg/dmg.h>
#include <dmg/adc.h>
#include <inttypes.h>

#define SECTORS_AT_A_TIME 0x800

typedef struct block {
	uint32_t idx;
	BLKXRun run;
	unsigned char* buf;
	size_t bufsize;
	ChecksumToken uncompressedChkToken;
	ChecksumToken compressedChkToken;

	struct block* next;
} block;

typedef struct {
	pthread_mutex_t mut;

	AbstractFile* in;
	bool useEOF;
	uint32_t sectorsRemain;
	const ChecksumAlgo* uncompressedChk;
	void* uncompressedChkToken;
	uint32_t curRun;
	uint64_t curSector;
} inData;

typedef struct {
	pthread_mutex_t mut;

	AbstractFile* out;
	const ChecksumAlgo* uncompressedChk;
	void* uncompressedChkToken;
	const ChecksumAlgo* compressedChk;
	void* compressedChkToken;
	BLKXTable* blkx;
	uint32_t roomForRuns;

	uint32_t nextIdx;
	block* pendingBlocks;
} outData;

typedef struct {
	inData in;
	outData out;
	size_t bufferSize;
} threadData;

static block* allocBlock(size_t bufferSize) {
	block* b;
	ASSERT(b = (block*) malloc(sizeof(block)), "malloc");
	ASSERT(b->buf = malloc(bufferSize), "malloc");
	return b;
}

static void freeBlock(block* b) {
	free(b->buf);
	free(b);
}

/* return false if there was nothing to read */
static bool readBlock(inData* i, block *inb) {
	size_t sectors, datasize;

	ASSERT(pthread_mutex_lock(&i->mut) == 0, "mutex lock");

	if (!i->useEOF && i->sectorsRemain == 0) {
		ASSERT(pthread_mutex_unlock(&i->mut) == 0, "mutex unlock");
		return false;
	}

	sectors = (i->sectorsRemain != 0 && i->sectorsRemain < SECTORS_AT_A_TIME) ? i->sectorsRemain : SECTORS_AT_A_TIME;
	datasize = sectors * SECTOR_SIZE;
	inb->bufsize = i->in->read(i->in, inb->buf, datasize);
	if (inb->bufsize < datasize) {
		ASSERT(i->useEOF && i->in->eof(i->in), "eof");
	}
	if (inb->bufsize == 0) {
		ASSERT(pthread_mutex_unlock(&i->mut) == 0, "mutex unlock");
		return false;
	}

	inb->run.reserved = 0;
	inb->run.sectorStart = i->curSector;
	inb->idx = i->curRun++;
	inb->run.sectorCount = inb->bufsize / SECTOR_SIZE;

	// printf("run %d: sectors=%" PRId64 ", left=%d\n", inb->idx, inb->run.sectorCount, i->sectorsRemain);

	if(i->uncompressedChk)
		i->uncompressedChk->serialOnly(i->uncompressedChkToken, inb->buf, inb->bufsize);

	i->curSector += inb->run.sectorCount;
	if (!i->useEOF)
		i->sectorsRemain -= inb->run.sectorCount;

	ASSERT(pthread_mutex_unlock(&i->mut) == 0, "mutex unlock");
	return true;
}

static void compressBlock(size_t bufferSize, const ChecksumAlgo* uncompressedChk, const ChecksumAlgo* compressedChk,
		block *inb, block *outb) {
	outb->idx = inb->idx;
	outb->run = inb->run;

	outb->bufsize = lzfse_encode_buffer(outb->buf, bufferSize, inb->buf, inb->bufsize, NULL);
	ASSERT(outb->bufsize > 0, "compression error");

	if (outb->bufsize > inb->bufsize) {
		outb->run.type = BLOCK_RAW;
		memcpy(outb->buf, inb->buf, inb->bufsize);
		outb->bufsize = inb->bufsize;
	} else {
		outb->run.type = BLOCK_LZFSE;
	}
	outb->run.compLength = outb->bufsize;

	if (uncompressedChk) {
		memset(&outb->uncompressedChkToken, 0, sizeof(outb->uncompressedChkToken));
		uncompressedChk->chunk(&outb->uncompressedChkToken, inb->buf, inb->bufsize);
	}
	if (compressedChk) {
		memset(&outb->compressedChkToken, 0, sizeof(outb->compressedChkToken));
		compressedChk->chunk(&outb->compressedChkToken, outb->buf, outb->bufsize);
	}
}

static void writeBlock(outData* o, block *outb) {
	outb->run.compOffset = o->out->tell(o->out) - o->blkx->dataStart;

	if(outb->idx >= o->roomForRuns) {
		o->roomForRuns <<= 1;
		o->blkx = (BLKXTable*) realloc(o->blkx, sizeof(BLKXTable) + (o->roomForRuns * sizeof(BLKXRun)));
	}
	o->blkx->runs[outb->idx] = outb->run;

	ASSERT(o->out->write(o->out, outb->buf, outb->bufsize) == outb->bufsize, "fwrite");

	if(o->uncompressedChk) {
		o->uncompressedChk->combine(o->uncompressedChkToken, &outb->uncompressedChkToken);
	}
	if(o->compressedChk) {
		o->compressedChk->serialOnly(o->compressedChkToken, outb->buf, outb->bufsize);
		o->compressedChk->combine(o->compressedChkToken, &outb->compressedChkToken);
	}
}

static void addBlockPending(outData* o, block *outb) {
	block** b;
	for (b = &o->pendingBlocks; *b && (*b)->idx < outb->idx; b = &(*b)->next)
		; /* pass */
	outb->next = *b;
	*b = outb;
}

static void writeBlocks(outData* o) {
	block* b;
	block* next;
	for (b = o->pendingBlocks; b && b->idx == o->nextIdx; b = next) {
		next = b->next;
		writeBlock(o, b);
		freeBlock(b);
		++o->nextIdx;
	}
	o->pendingBlocks = b;
}

static void finishBlock(size_t bufferSize, outData* o, block *outb) {
	block* b;

	ASSERT(pthread_mutex_lock(&o->mut) == 0, "mutex lock");

	addBlockPending(o, outb);
	writeBlocks(o);

	ASSERT(pthread_mutex_unlock(&o->mut) == 0, "mutex unlock");
}

static void* threadWorker(void* arg) {
	threadData* d;
	block *inb, *outb;

	d = (threadData*)arg;
	inb = allocBlock(d->bufferSize);
	outb = allocBlock(d->bufferSize);

	while(true) {
		if (!readBlock(&d->in, inb))
			break;

		compressBlock(d->bufferSize, d->in.uncompressedChk, d->out.compressedChk, inb, outb);
		finishBlock(d->bufferSize, &d->out, outb);
		outb = allocBlock(d->bufferSize);
	}

	freeBlock(inb);
	freeBlock(outb);

	return NULL;
}

BLKXTable* insertBLKX(AbstractFile* out_, AbstractFile* in_, uint32_t firstSectorNumber, uint32_t numSectors_, uint32_t blocksDescriptor,
			uint32_t checksumType, const ChecksumAlgo* uncompressedChk_, void* uncompressedChkToken_, const ChecksumAlgo* compressedChk_,
			void* compressedChkToken_, Volume* volume, int zlibLevel) {
	threadData td;
	size_t i, nthreads;
	pthread_t *threads;
	void* ret;

	td.in.in = in_;
	td.in.useEOF = (numSectors_ == 0);
	td.in.sectorsRemain = numSectors_;
	td.in.uncompressedChk = uncompressedChk_;
	td.in.uncompressedChkToken = uncompressedChkToken_;
	td.in.curRun = 0;
	td.in.curSector = 0;
	pthread_mutex_init(&td.in.mut, NULL);

	td.out.out = out_;
	td.out.uncompressedChk = uncompressedChk_;
	td.out.uncompressedChkToken = uncompressedChkToken_;
	td.out.compressedChk = compressedChk_;
	td.out.compressedChkToken = compressedChkToken_;
	td.out.nextIdx = 0;
	td.out.pendingBlocks = NULL;
	pthread_mutex_init(&td.out.mut, NULL);

	td.out.blkx = (BLKXTable*) malloc(sizeof(BLKXTable) + (2 * sizeof(BLKXRun)));
	td.out.roomForRuns = 2;
	memset(td.out.blkx, 0, sizeof(BLKXTable) + (td.out.roomForRuns * sizeof(BLKXRun)));

	td.out.blkx->fUDIFBlocksSignature = UDIF_BLOCK_SIGNATURE;
	td.out.blkx->infoVersion = 1;
	td.out.blkx->firstSectorNumber = firstSectorNumber;
	td.out.blkx->dataStart = 0;
	td.out.blkx->decompressBufferRequested = SECTORS_AT_A_TIME + 8;
	td.out.blkx->blocksDescriptor = blocksDescriptor;
	td.out.blkx->reserved1 = 0;
	td.out.blkx->reserved2 = 0;
	td.out.blkx->reserved3 = 0;
	td.out.blkx->reserved4 = 0;
	td.out.blkx->reserved5 = 0;
	td.out.blkx->reserved6 = 0;
	memset(&(td.out.blkx->checksum), 0, sizeof(td.out.blkx->checksum));
	td.out.blkx->checksum.type = checksumType;
	td.out.blkx->checksum.size = 0x20;
	td.out.blkx->blocksRunCount = 0;

	td.bufferSize = SECTOR_SIZE * td.out.blkx->decompressBufferRequested;

	nthreads = sysconf(_SC_NPROCESSORS_ONLN) + 2; /* input + output */
	ASSERT(threads = (pthread_t*) malloc(nthreads * sizeof(pthread_t)), "malloc");
	for (i = 0; i < nthreads; ++i)
		ASSERT(pthread_create(&threads[i], NULL, &threadWorker, &td) == 0, "thread create");

	for (i = 0; i < nthreads; ++i) {
		ASSERT(pthread_join(threads[i], &ret) == 0, "thread join");
		ASSERT(ret == NULL, "thread return");
	}
	free(threads);

	if(td.in.curRun >= td.out.roomForRuns) {
		td.out.roomForRuns <<= 1;
		td.out.blkx = (BLKXTable*) realloc(td.out.blkx, sizeof(BLKXTable) + (td.out.roomForRuns * sizeof(BLKXRun)));
	}

	td.out.blkx->runs[td.in.curRun].type = BLOCK_TERMINATOR;
	td.out.blkx->runs[td.in.curRun].reserved = 0;
	td.out.blkx->runs[td.in.curRun].sectorStart = td.in.curSector;
	td.out.blkx->runs[td.in.curRun].sectorCount = 0;
	td.out.blkx->runs[td.in.curRun].compOffset = td.out.out->tell(td.out.out) - td.out.blkx->dataStart;
	td.out.blkx->runs[td.in.curRun].compLength = 0;
	td.out.blkx->blocksRunCount = td.in.curRun + 1;
	td.out.blkx->sectorCount = td.in.curSector;

	return td.out.blkx;
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
