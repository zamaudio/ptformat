/*
 * libptformat - a library to read ProTools sessions
 *
 * Copyright (C) 2015  Damien Zammit
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "ptfformat.h"
#include <inttypes.h> // PRIxyy
#include <cstdio>

using namespace std;
using std::string;

int main (int argc, char **argv) {
	PTFFormat ptf;
	int ok;

	if (argc == 1) {
		printf("No ptf file specified, quit\n");
		exit(0);
	}

	ok = ptf.load(argv[1], 48000);

	switch (ok) {
	default:
	case -1:
		printf("Cannot open ptf, quit\n");
		exit(-1);
		break;
	case 0:
		printf("ProTools %d Session: Samplerate = %" PRId64 "Hz\nTarget samplerate = 48000\n\n", ptf.version, ptf.sessionrate);
		printf("%zu wavs, %zu regions, %zu active regions\n\n",
			ptf.audiofiles.size(),
			ptf.regions.size(),
			ptf.tracks.size()
			);
		printf("Audio file (WAV#) @ offset, length:\n");
		for (vector<PTFFormat::wav_t>::iterator
				a = ptf.audiofiles.begin();
				a != ptf.audiofiles.end(); ++a) {
			printf("`%s` w(%d) @ %" PRIu64 ", %" PRIu64 "\n",
				a->filename.c_str(),
				a->index,
				a->posabsolute,
				a->length);
		}

		printf("\nRegion (Region#) (WAV#) @ into-sample, length:\n");
		for (vector<PTFFormat::region_t>::iterator
				a = ptf.regions.begin();
				a != ptf.regions.end(); ++a) {
			printf("`%s` r(%d) w(%d) @ %" PRIu64 ", %" PRIu64 "\n",
				a->name.c_str(),
				a->index,
				a->wave.index,
				a->sampleoffset,
				a->length);
		}

		printf("\nMIDI Region (Region#) @ into-sample, length:\n");
		for (vector<PTFFormat::region_t>::iterator
			a = ptf.midiregions.begin();
			a != ptf.midiregions.end(); ++a) {
			printf("`%s` r(%d) @ %" PRIu64 ", %" PRIu64 "\n",
				a->name.c_str(),
				a->index,
				a->sampleoffset,
				a->length);
			for (vector<PTFFormat::midi_ev_t>::iterator
					b = a->midi.begin();
					b != a->midi.end(); ++b) {
				printf("    MIDI: n(%d) v(%d) @ %" PRIu64 ", %" PRIu64 "\n",
					b->note, b->velocity,
					b->pos, b->length);
			}
		}

		printf("\nTrack name (Track#) (Region#) @ Absolute:\n");
		for (vector<PTFFormat::track_t>::iterator
				a = ptf.tracks.begin();
				a != ptf.tracks.end(); ++a) {
			printf("`%s` t(%d) r(%d) @ %" PRIu64 "\n",
				a->name.c_str(),
				a->index,
				a->reg.index,
				a->reg.startpos);
		}

		printf("\nMIDI Track name (MIDITrack#) (MIDIRegion#) @ Absolute:\n");
		for (vector<PTFFormat::track_t>::iterator
				a = ptf.miditracks.begin();
				a != ptf.miditracks.end(); ++a) {
			printf("`%s` mt(%d) mr(%d) @ %" PRIu64 "\n",
				a->name.c_str(),
				a->index,
				a->reg.index,
				a->reg.startpos);
		}

		printf("\nTrack name (Track#) (WAV filename) @ Absolute + Into-sample, Length:\n");
		for (vector<PTFFormat::track_t>::iterator
				a = ptf.tracks.begin();
				a != ptf.tracks.end(); ++a) {
			printf("`%s` t(%d) (%s) @ %" PRIu64 " + %" PRIu64 ", %" PRIu64 "\n",
				a->name.c_str(),
				a->index,
				a->reg.wave.filename.c_str(),
				a->reg.startpos,
				a->reg.sampleoffset,
				a->reg.length
				);
		}
		break;
	}
	exit(0);
}
