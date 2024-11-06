#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <dmg/dmg.h>
#include <dmg/filevault.h>

char endianness;

void TestByteOrder()
{
	short int word = 0x0001;
	char *byte = (char *) &word;
	endianness = byte[0] ? IS_LITTLE_ENDIAN : IS_BIG_ENDIAN;
}

int buildInOut(const char* source, const char* dest, AbstractFile** in, AbstractFile** out) {
	*in = createAbstractFileFromFile(fopen(source, "rb"));
	if(!(*in)) {
		printf("cannot open source: %s\n", source);
		return FALSE;
	}

	*out = createAbstractFileFromFile(strcmp(dest, "-") == 0 ? stdout : fopen(dest, "wb"));
	if(!(*out)) {
		(*in)->close(*in);
		printf("cannot open destination: %s\n", dest);
		return FALSE;
	}

	return TRUE;
}

int usage(const char *name) {
	printf("usage: %s [extract|build|build2048|res|iso|dmg|attribute] <in> <out> (-k <key>) (partition)\n", name);
	return 2;
}

int main(int argc, char* argv[]) {
	int partNum;
	AbstractFile* in;
	AbstractFile* out;
	int opt;
	char *cmd, *infile, *outfile;
	char *key = NULL;
	
	TestByteOrder();

	while ((opt = getopt(argc, argv, "k:")) != -1) {
		switch (opt) {
			case 'k':
				key = optarg;
				break;
			default:
				return usage(argv[0]);
		}
	}

	if (argc < optind + 3) {
		return usage(argv[0]);
	}
	cmd = argv[optind++];
	infile = argv[optind++];
	outfile = argv[optind++];

	if(!buildInOut(infile, outfile, &in, &out)) {
		return -1;
	}
	if(key != NULL) {
		in = createAbstractFileFromFileVault(in, key);
	}

	if(strcmp(cmd, "extract") == 0) {
		partNum = -1;
		if (optind < argc) {
				sscanf(argv[optind++], "%d", &partNum);
		}
		extractDmg(in, out, partNum);
	} else if(strcmp(cmd, "build") == 0) {
		char *anchor = NULL;
		if (argc >= optind) {
			anchor = argv[optind++];
		}
		buildDmg(in, out, SECTOR_SIZE, anchor);
	} else if(strcmp(cmd, "build2048") == 0) {
		buildDmg(in, out, 2048, NULL);
	} else if(strcmp(cmd, "res") == 0) {
		outResources(in, out);
	} else if(strcmp(cmd, "iso") == 0) {
		convertToISO(in, out);
	} else if(strcmp(cmd, "dmg") == 0) {
		convertToDMG(in, out);
	} else if(strcmp(cmd, "attribute") == 0) {
		char *anchor, *data;
		if(argc < optind + 2) {
			printf("Not enough arguments: attribute <in> <out> <sentinel> <string>");
			return 2;
		}
		anchor = argv[optind++];
		data = argv[optind++];
		updateAttribution(in, out, anchor, data, strlen(data));
	}

	return 0;
}
