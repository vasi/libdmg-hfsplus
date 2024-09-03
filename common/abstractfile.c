#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>

#include "abstractfile.h"
#include "common.h"

size_t freadWrapper(AbstractFile* file, void* data, size_t len) {
  return fread(data, 1, len, (FILE*) (file->data));
}

size_t fwriteWrapper(AbstractFile* file, const void* data, size_t len) {
  return fwrite(data, 1, len, (FILE*) (file->data));
}

int fseekWrapper(AbstractFile* file, off_t offset) {
  return fseeko((FILE*) (file->data), offset, SEEK_SET);
}

off_t ftellWrapper(AbstractFile* file) {
  return ftello((FILE*) (file->data));
}

void fcloseWrapper(AbstractFile* file) {
  fclose((FILE*) (file->data));
  free(file);
}

off_t fileGetLength(AbstractFile* file) {
	off_t length;
	off_t pos;

	pos = ftello((FILE*) (file->data));

	fseeko((FILE*) (file->data), 0, SEEK_END);
	length = ftello((FILE*) (file->data));

	fseeko((FILE*) (file->data), pos, SEEK_SET);

	return length;
}

int fileEOF(AbstractFile* file) {
	return feof((FILE*) (file->data));
}

int isPipe(FILE* file) {
	int ret = fseeko(file, 0, SEEK_CUR);
	return (ret != 0);
}

#define STDIO_NAME "-"

AbstractFile* createAbstractFileFromArg(const char* arg, const char* mode) {
	FILE* file;

	if (strcmp(STDIO_NAME, arg) == 0) {
		if (index(mode, 'w') != NULL)
			file = stdout;
		else
			file = stdin;
	} else {
		file = fopen(arg, mode);
	}
	return createAbstractFileFromFile(file);
}

AbstractFile* createAbstractFileFromFile(FILE* file) {
	AbstractFile* toReturn;

	if(file == NULL) {
		return NULL;
	}

	if (isPipe(file)) {
		return createAbstractFileFromPipe(file, 0);
	}

	toReturn = (AbstractFile*) malloc(sizeof(AbstractFile));
	toReturn->data = file;
	toReturn->read = freadWrapper;
	toReturn->write = fwriteWrapper;
	toReturn->seek = fseekWrapper;
	toReturn->tell = ftellWrapper;
	toReturn->getLength = fileGetLength;
	toReturn->close = fcloseWrapper;
	toReturn->eof = fileEOF;
	toReturn->type = AbstractFileTypeFile;
	return toReturn;
}

size_t dummyRead(AbstractFile* file, void* data, size_t len) {
  return 0;
}

size_t dummyWrite(AbstractFile* file, const void* data, size_t len) {
  *((off_t*) (file->data)) += len;
  return len;
}

int dummySeek(AbstractFile* file, off_t offset) {
  *((off_t*) (file->data)) = offset;
  return 0;
}

off_t dummyTell(AbstractFile* file) {
  return *((off_t*) (file->data));
}

int dummyEOF(AbstractFile* file) {
	return 0;
}

void dummyClose(AbstractFile* file) {
  free(file);
}

AbstractFile* createAbstractFileFromDummy() {
	AbstractFile* toReturn;
	toReturn = (AbstractFile*) malloc(sizeof(AbstractFile));
	toReturn->data = NULL;
	toReturn->read = dummyRead;
	toReturn->write = dummyWrite;
	toReturn->seek = dummySeek;
	toReturn->tell = dummyTell;
	toReturn->getLength = NULL;
	toReturn->close = dummyClose;
	toReturn->eof = dummyEOF;
	toReturn->type = AbstractFileTypeDummy;
	return toReturn;
}

size_t memRead(AbstractFile* file, void* data, size_t len) {
  MemWrapperInfo* info = (MemWrapperInfo*) (file->data);
  if(info->bufferSize < (info->offset + len)) {
    len = info->bufferSize - info->offset;
  }
  memcpy(data, (void*)((uint8_t*)(*(info->buffer)) + (uint32_t)info->offset), len);
  info->offset += (size_t)len;
  return len;
}

size_t memWrite(AbstractFile* file, const void* data, size_t len) {
  MemWrapperInfo* info = (MemWrapperInfo*) (file->data);

  while((info->offset + (size_t)len) > info->bufferSize) {
    info->bufferSize <<= 1;
    *(info->buffer) = realloc(*(info->buffer), info->bufferSize);
  }

  memcpy((void*)((uint8_t*)(*(info->buffer)) + (uint32_t)info->offset), data, len);
  info->offset += (size_t)len;
  return len;
}

int memSeek(AbstractFile* file, off_t offset) {
  MemWrapperInfo* info = (MemWrapperInfo*) (file->data);
  info->offset = (size_t)offset;
  return 0;
}

off_t memTell(AbstractFile* file) {
  MemWrapperInfo* info = (MemWrapperInfo*) (file->data);
  return (off_t)info->offset;
}

off_t memGetLength(AbstractFile* file) {
  MemWrapperInfo* info = (MemWrapperInfo*) (file->data);
  return info->bufferSize;
}

int memEOF(AbstractFile* file) {
	MemWrapperInfo* info = (MemWrapperInfo*) (file->data);
	return info->offset >= info->bufferSize;
}

void memClose(AbstractFile* file) {
  free(file->data);
  free(file);
}

AbstractFile* createAbstractFileFromMemory(void** buffer, size_t size) {
	MemWrapperInfo* info;
	AbstractFile* toReturn;
	toReturn = (AbstractFile*) malloc(sizeof(AbstractFile));

	info = (MemWrapperInfo*) malloc(sizeof(MemWrapperInfo));
	info->offset = 0;
	info->buffer = buffer;
	info->bufferSize = size;

	toReturn->data = info;
	toReturn->read = memRead;
	toReturn->write = memWrite;
	toReturn->seek = memSeek;
	toReturn->tell = memTell;
	toReturn->getLength = memGetLength;
	toReturn->close = memClose;
	toReturn->eof = memEOF;
	toReturn->type = AbstractFileTypeMem;
	return toReturn;
}

void abstractFilePrint(AbstractFile* file, const char* format, ...) {
	va_list args;
	char buffer[1024];
	size_t length;

	buffer[0] = '\0';
	va_start(args, format);
	length = vsprintf(buffer, format, args);
	va_end(args);
	ASSERT(file->write(file, buffer, length) == length, "fwrite");
}

int absFileRead(io_func* io, off_t location, size_t size, void *buffer) {
	AbstractFile* file;
	file = (AbstractFile*) io->data;
	file->seek(file, location);
	if(file->read(file, buffer, size) == size) {
		return TRUE;
	} else {
		return FALSE;
	}
}

int absFileWrite(io_func* io, off_t location, size_t size, void *buffer) {
	AbstractFile* file;
	file = (AbstractFile*) io->data;
	file->seek(file, location);
	if(file->write(file, buffer, size) == size) {
		return TRUE;
	} else {
		return FALSE;
	}
}

void closeAbsFile(io_func* io) {
	AbstractFile* file;
	file = (AbstractFile*) io->data;
	file->close(file);
	free(io);
}


io_func* IOFuncFromAbstractFile(AbstractFile* file) {
	io_func* io;

	io = (io_func*) malloc(sizeof(io_func));
	io->data = file;
	io->read = &absFileRead;
	io->write = &absFileWrite;
	io->close = &closeAbsFile;

	return io;
}

size_t memFileRead(AbstractFile* file, void* data, size_t len) {
  MemFileWrapperInfo* info = (MemFileWrapperInfo*) (file->data);
  memcpy(data, (void*)((uint8_t*)(*(info->buffer)) + (uint32_t)info->offset), len);
  info->offset += (size_t)len;
  return len;
}

size_t memFileWrite(AbstractFile* file, const void* data, size_t len) {
  MemFileWrapperInfo* info = (MemFileWrapperInfo*) (file->data);

  while((info->offset + (size_t)len) > info->actualBufferSize) {
		info->actualBufferSize <<= 1;
    *(info->buffer) = realloc(*(info->buffer), info->actualBufferSize);
  }

  if((info->offset + (size_t)len) > (*(info->bufferSize))) {
		*(info->bufferSize) = info->offset + (size_t)len;
	}

  memcpy((void*)((uint8_t*)(*(info->buffer)) + (uint32_t)info->offset), data, len);
  info->offset += (size_t)len;
  return len;
}

int memFileSeek(AbstractFile* file, off_t offset) {
  MemFileWrapperInfo* info = (MemFileWrapperInfo*) (file->data);
  info->offset = (size_t)offset;
  return 0;
}

off_t memFileTell(AbstractFile* file) {
  MemFileWrapperInfo* info = (MemFileWrapperInfo*) (file->data);
  return (off_t)info->offset;
}

off_t memFileGetLength(AbstractFile* file) {
  MemFileWrapperInfo* info = (MemFileWrapperInfo*) (file->data);
  return *(info->bufferSize);
}

int memFileEOF(AbstractFile* file) {
	MemFileWrapperInfo* info = (MemFileWrapperInfo*) (file->data);
	return info->offset >= *(info->bufferSize);
}

void memFileClose(AbstractFile* file) {
  free(file->data);
  free(file);
}

AbstractFile* createAbstractFileFromMemoryFile(void** buffer, size_t* size) {
	MemFileWrapperInfo* info;
	AbstractFile* toReturn;
	toReturn = (AbstractFile*) malloc(sizeof(AbstractFile));

	info = (MemFileWrapperInfo*) malloc(sizeof(MemFileWrapperInfo));
	info->offset = 0;
	info->buffer = buffer;
	info->bufferSize = size;
	info->actualBufferSize = (1024 < (*size)) ? (*size) : 1024;
	if(info->actualBufferSize != *(info->bufferSize)) {
		*(info->buffer) = realloc(*(info->buffer), info->actualBufferSize);
	}

	toReturn->data = info;
	toReturn->read = memFileRead;
	toReturn->write = memFileWrite;
	toReturn->seek = memFileSeek;
	toReturn->tell = memFileTell;
	toReturn->getLength = memFileGetLength;
	toReturn->close = memFileClose;
	toReturn->eof = memFileEOF;
	toReturn->type = AbstractFileTypeMemFile;
	return toReturn;
}

AbstractFile* createAbstractFileFromMemoryFileBuffer(void** buffer, size_t* size, size_t actualBufferSize) {
	MemFileWrapperInfo* info;
	AbstractFile* toReturn;
	toReturn = (AbstractFile*) malloc(sizeof(AbstractFile));

	info = (MemFileWrapperInfo*) malloc(sizeof(MemFileWrapperInfo));
	info->offset = 0;
	info->buffer = buffer;
	info->bufferSize = size;
	info->actualBufferSize = actualBufferSize;

	toReturn->data = info;
	toReturn->read = memFileRead;
	toReturn->write = memFileWrite;
	toReturn->seek = memFileSeek;
	toReturn->tell = memFileTell;
	toReturn->getLength = memFileGetLength;
	toReturn->close = memFileClose;
	toReturn->type = AbstractFileTypeMemFile;
	return toReturn;
}

typedef struct {
	FILE* file;
	off_t offset; /* logical offset */
	unsigned char* startBuf;
	size_t startSize;
} PipeInfo;

size_t pipeRead(AbstractFile* file, void* data, size_t len) {
	PipeInfo* info;
	size_t read, n;

	info = (PipeInfo*) (file->data);
	read = 0;
	
	if (info->offset < info->startSize) {
		n = info->startSize - info->offset;
		if (n > len)
			n = len;

		memcpy(data, info->startBuf + info->offset, n);
		info->offset += n;
		len -= n;
		read += n;
	}
	if (len > 0) {
	  n = fread(data, 1, len, info->file);
		info->offset += n;
		read += n;
	}
	return read;
}

size_t pipeWrite(AbstractFile* file, const void* data, size_t len) {
	return -1;
}

int pipeSkipTo(PipeInfo* info, off_t offset) {
	char buf[4096];
	size_t len, read;

	while (info->offset < offset) {
		len = (sizeof(buf) + info->offset > offset) ? (offset - info->offset) : sizeof(buf);
		read = fread(buf, 1, len, info->file);
		info->offset += read;
		if (read != len)
			return -1;
	}
	return 0;
}

int pipeSeek(AbstractFile* file, off_t offset) {
	PipeInfo* info;
	int ret;

	info = (PipeInfo*) (file->data);
	
	if (offset == info->offset) {
		return 0;
	} else if (offset < info->offset) {
	  if (info->offset > info->startSize) {
			/* we may lose data if we seek back here */
			errno = ESPIPE;
			return -1;
		} else {
			info->offset = offset;
			return 0;
		}
	} else {
		return pipeSkipTo(info, offset);
	}
}

off_t pipeTell(AbstractFile* file) {
	PipeInfo* info = (PipeInfo*) (file->data);
	return info->offset;
}

void pipeClose(AbstractFile* file) {
	PipeInfo* info = (PipeInfo*) (file->data);
	fclose(info->file);
	free(info->startBuf);
	free(info);
	free(file);
}

off_t pipeGetLength(AbstractFile* file) {
	return -1;
}

int pipeEOF(AbstractFile* file) {
	PipeInfo* info = (PipeInfo*) (file->data);
	return (info->offset >= info->startSize) && feof(info->file);
}

#define PIPE_DEFAULT_BUFFER_SIZE (10 * 1024 * 1024)

AbstractFile* createAbstractFileFromPipe(FILE* file, size_t bufferSize) {
	AbstractFile* toReturn;
	PipeInfo* info;

	if(file == NULL) {
		return NULL;
	}

	ASSERT(info = (PipeInfo*) malloc(sizeof(PipeInfo)), "malloc");
	info->file = file;
	info->offset = 0;
	if (bufferSize == 0) {
		bufferSize = PIPE_DEFAULT_BUFFER_SIZE;
	}
	ASSERT(info->startBuf = malloc(bufferSize), "malloc");
	info->startSize = fread(info->startBuf, 1, bufferSize, info->file);

	ASSERT(toReturn = (AbstractFile*) malloc(sizeof(AbstractFile)), "malloc");
	toReturn->data = info;
	toReturn->read = pipeRead;
	toReturn->write = pipeWrite;
	toReturn->seek = pipeSeek;
	toReturn->tell = pipeTell;
	toReturn->getLength = pipeGetLength;
	toReturn->close = pipeClose;
	toReturn->eof = pipeEOF;
	toReturn->type = AbstractFileTypePipe;
	return toReturn;
}
