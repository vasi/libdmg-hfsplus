#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dmg/dmg.h>
#include <dmg/filevault.h>
#include <zlib.h>

char endianness;

void TestByteOrder()
{
	short int word = 0x0001;
	char *byte = (char *) &word;
	endianness = byte[0] ? IS_LITTLE_ENDIAN : IS_BIG_ENDIAN;
}

int buildInOut(const char* source, const char* dest, AbstractFile** in, AbstractFile** out) {
	*in = createAbstractFileFromArg(source, "rb");
	if(!(*in)) {
		printf("cannot open source: %s\n", source);
		return FALSE;
	}

	*out = createAbstractFileFromArg(dest, "wb");
	if(!(*out)) {
		(*in)->close(*in);
		printf("cannot open destination: %s\n", dest);
		return FALSE;
	}

	return TRUE;
}

int main(int argc, char* argv[]) {
	int partNum;
	int zlibLevel = Z_DEFAULT_COMPRESSION;
	int argIndex = 4;
	AbstractFile* in;
	AbstractFile* out;

	TestByteOrder();

	if(argc < 4) {
		printf("usage: %s [extract|build|iso|dmg] <in> <out> (-k <key>) (-zlib-level <0-9>) (partition)\n", argv[0]);
		return 0;
	}

	if(!buildInOut(argv[2], argv[3], &in, &out)) {
		return 1;
	}

	while (argv[argIndex] != NULL) {
		if(strcmp(argv[argIndex], "-k") == 0) {
			in = createAbstractFileFromFileVault(in, argv[argIndex+1]);
			if(in == NULL) {
				fprintf(stderr, "error: Cannot open image-file.\n");
				return 1;
			}
			argIndex += 2;
		} else if (strcmp(argv[argIndex], "-zlib-level") == 0) {
			char* zlibLevel_str = argv[argIndex+1];
			if (!zlibLevel_str) {
				fprintf(stderr, "error: no zlib-level provided\n");
				return 1;
			}
			char* endptr;
			zlibLevel = (int)strtol(zlibLevel_str, &endptr, 10);
			if (endptr != (zlibLevel_str+1)) {
				fprintf(stderr, "error: invalid zlib-level provided\n");
				return 1;
			}
			argIndex += 2;
		} else break;
	}

	if(strcmp(argv[1], "extract") == 0) {
		partNum = -1;
		sscanf(argv[argIndex], "%d", &partNum);
		extractDmg(in, out, partNum);
	} else if(strcmp(argv[1], "build") == 0) {
		buildDmg(in, out, zlibLevel);
	} else if(strcmp(argv[1], "iso") == 0) {
		convertToISO(in, out);
	} else if(strcmp(argv[1], "dmg") == 0) {
		convertToDMG(in, out, zlibLevel);
	}

	return 0;
}
