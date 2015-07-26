/*
    Copyright (C) 2015  Damien Zammit

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

*/

#include "ptfformat.h"
using namespace std;
using std::string;

int main (int argc, char **argv) {
	PTFFormat ptf;
	int ok;

	if (argc == 1) {
		printf("No ptf file specified, quit\n");
		exit(0);
	}

	ok = ptf.load(argv[1]);

	switch (ok) {
	case -1:
		printf("Cannot open ptf, quit\n");
		exit(-1);
		break;
	case 0:
		if (ptf.audiofiles.size() > 0) {
			printf("Audio file recorded @ offset, length:\n");
			for (vector<PTFFormat::files_t>::iterator
					a = ptf.audiofiles.begin();
					a != ptf.audiofiles.end(); ++a) {
				//printf("%s @ 0x%08x + 0x%08x, len=0x%08x\n", a->filename.c_str(),
				printf("%s @ %lu, len=%lu\n", a->filename.c_str(),
					a->posabsolute, a->length);
			}
			printf("\nRegion @ absolute + into-sample, length:\n");
			for (vector<PTFFormat::files_t>::iterator
					a = ptf.regions.begin();
					a != ptf.regions.end(); ++a) {
				//printf("%s @ 0x%08x + 0x%08x, len=0x%08x\n", a->filename.c_str(),
				printf("%s @ %lu + %lu, len=%lu\n", a->filename.c_str(),
					a->posabsolute, a->sampleoffset, a->length);
			}
		} else {
			printf("No audio files in session, quit\n");
		}
		break;
	default:
		printf("Missing LUT 0x%02x, quit\n", ok);
		exit(ok);
		break;
	}
	exit(0);
}
