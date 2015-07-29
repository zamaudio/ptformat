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
		printf("PT8 Session: Samplerate = %dHz\n\n", ptf.sessionrate);
		if (ptf.audiofiles.size() > 0) {
			printf("%d wavs, %d regions, %d tracks\n\n",
				ptf.audiofiles.size(),
				ptf.regions.size(),
				ptf.tracks.size()
				);
			printf("Audio file (WAV#) @ offset, length:\n");
			for (vector<PTFFormat::wav_t>::iterator
					a = ptf.audiofiles.begin();
					a != ptf.audiofiles.end(); ++a) {
				//printf("%s @ %lu, len=%lu\n", a->filename.c_str(),
				printf("`%s` w(%d) @ 0x%08x, len=0x%08x\n",
					a->filename.c_str(),
					a->index,
					a->posabsolute,
					a->length);
			}
			
			printf("\nRegion (Region#) (WAV#) @ into-sample, length:\n");
			for (vector<PTFFormat::region_t>::iterator
					a = ptf.regions.begin();
					a != ptf.regions.end(); ++a) {
				//printf("%s (%s) @ %lu + %lu, len=%lu\n", a->name.c_str(),
				printf("`%s` r(%d) w(%d) @ 0x%08x, len=0x%08x\n",
					a->name.c_str(),
					a->index,
					a->wave.index,
					a->sampleoffset,
					a->length);
			}

			printf("\nTrack name (Track#) (Playlist#) (Region#) @ Absolute:\n");
			for (vector<PTFFormat::track_t>::iterator
					a = ptf.tracks.begin();
					a != ptf.tracks.end(); ++a) {
				//printf("%s (%s) @ %lu + %lu, len=%lu\n", a->name.c_str(),
				printf("`%s` t(%d) p(%d) r(%d) @ 0x%08x\n",
					a->name.c_str(),
					a->index,
					a->playlist,
					a->reg.index,
					a->reg.startpos);
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
