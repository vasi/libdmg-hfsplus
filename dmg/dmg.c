#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
	(*in)->printSeeks = 1;

	*out = createAbstractFileFromFile(strcmp(dest, "-") == 0 ? stdout : fopen(dest, "wb"));
	if(!(*out)) {
		(*in)->close(*in);
		printf("cannot open destination: %s\n", dest);
		return FALSE;
	}

	return TRUE;
}

void usage(const char *name) {
	printf("usage: %s (OPTIONS) [extract|build|build2048|res|iso|dmg|attribute] <in> <out> (partition)\n", name);
	printf("OPTIONS:\n");
	printf("\t-k\tkey\n");
	printf("\t-J\tcompressor name (%s)\n", compressionNames());
	printf("\t-L\tcompression level\n");
	printf("\t-r\trun size (in sectors)\n");
	exit(2);
}

/* Mini getopt. Contrary to macOS's getopt, allow options after positionals */
static int moptind = 0;
static int moptdst, moptdone;
static char *moptarg;

int mgetopt(int argc, char** argv, char *optstr)
{
	char *opt;
	int i;

	if (moptind == 0) { /* setup */
		moptind = 1;
		moptdst = 1; /* position in argv next arg will be moved to */
		moptdone = 0; /* when we've seen -- */
	}

	while (moptind < argc) {
		char *opt = argv[moptind++];
		if (moptdone || opt[0] != '-' || strcmp(opt, "-") == 0) { /* - can be positional */
			argv[moptdst++] = opt; /* positional arg, move it before options */
			continue;
		}
		if (strcmp(opt, "--") == 0) {
			moptdone = 1;
			continue;
		}

		if (moptind >= argc || strlen(opt) != 2 || strchr(optstr, opt[1]) == NULL) {
			usage(argv[0]);
		}

		moptarg = argv[moptind++];
		return opt[1];
	}

	/* done with options, put positions args such that they end at argc */
	for (i = 1; i < moptdst; i++) {
		argv[argc - i] = argv[moptdst - i];
	}
	moptind = argc - (moptdst - 1);
	return -1;
}

int main(int argc, char* argv[]) {
	int partNum;
	AbstractFile* in;
	AbstractFile* out;
	int opt;
	char *cmd, *infile, *outfile;
	char *key = NULL;
	Compressor comp;
	int ret;
	int runSectors = DEFAULT_SECTORS_AT_A_TIME;

	TestByteOrder();
	getCompressor(&comp, NULL);

	while ((opt = mgetopt(argc, argv, "kJLr")) != -1) {
		switch (opt) {
			case 'k':
				key = moptarg;
				break;
			case 'J':
				ret = getCompressor(&comp, moptarg);
				if (ret != 0) {
					fprintf(stderr, "Unknown compressor \"%s\"\nAllowed options are: %s\n", moptarg, compressionNames());
					return 2;
				}
				break;
			case 'L':
				sscanf(moptarg, "%d", &comp.level);
				break;
			case 'r':
				sscanf(moptarg, "%d", &runSectors);
				if (runSectors < DEFAULT_SECTORS_AT_A_TIME) {
					fprintf(stderr, "Run size must be at least %d sectors\n", DEFAULT_SECTORS_AT_A_TIME);
					return 2;
				}
				break;
		}
	}

	if (argc < moptind + 3) {
		usage(argv[0]);
	}
	cmd = argv[moptind++];
	infile = argv[moptind++];
	outfile = argv[moptind++];

	if(!buildInOut(infile, outfile, &in, &out)) {
		return -1;
	}
	if(key != NULL) {
		in = createAbstractFileFromFileVault(in, key);
	}

	if(strcmp(cmd, "extract") == 0) {
		partNum = -1;
		if (moptind < argc) {
				sscanf(argv[moptind++], "%d", &partNum);
		}
		extractDmg(in, out, partNum);
	} else if(strcmp(cmd, "build") == 0) {
		char *anchor = NULL;
		if (moptind < argc) {
			anchor = argv[moptind++];
		}
		buildDmg(in, out, SECTOR_SIZE, anchor, &comp, runSectors);
	} else if(strcmp(cmd, "build2048") == 0) {
		buildDmg(in, out, 2048, NULL, &comp, runSectors);
	} else if(strcmp(cmd, "res") == 0) {
		outResources(in, out);
	} else if(strcmp(cmd, "iso") == 0) {
		convertToISO(in, out);
	} else if(strcmp(cmd, "dmg") == 0) {
		convertToDMG(in, out, &comp, runSectors);
	} else if(strcmp(cmd, "attribute") == 0) {
		char *anchor, *data;
		if(argc < moptind + 2) {
			printf("Not enough arguments: attribute <in> <out> <sentinel> <string>");
			return 2;
		}
		anchor = argv[moptind++];
		data = argv[moptind++];
		updateAttribution(in, out, anchor, data, strlen(data));
	}

	return 0;
}
