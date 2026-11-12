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

/*
 * ptlevel: write a copy of a session with the level or the mute of a
 * mixer slot of a track changed.
 *
 * Usage: ptlevel in.ptx out.ptx track slot dB
 *        ptlevel --mute|--unmute in.ptx out.ptx track slot
 *
 * slot is "out" for the track's output (its fader) or a letter A to J
 * for a send. dB runs from -80 to +12. The session read is never
 * written: out must be a new file. A slot whose level or mute holds
 * automation is refused.
 *
 * Exit status: 0 success, 1 no such track or slot, 2 usage, read,
 * write error or refused change.
 */

#include "ptformat/ptformat.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace std;

int main (int argc, char **argv) {
	int mute = -1;
	int arg = 1;
	if (arg < argc && strcmp(argv[arg], "--mute") == 0) {
		mute = 1;
		arg++;
	} else if (arg < argc && strcmp(argv[arg], "--unmute") == 0) {
		mute = 0;
		arg++;
	}
	if (argc - arg != (mute < 0 ? 5 : 4)) {
		fprintf(stderr, "Usage: %s in.ptx out.ptx track out|A-J dB\n"
			"       %s --mute|--unmute in.ptx out.ptx track out|A-J\n", argv[0], argv[0]);
		return 2;
	}
	const char *in = argv[arg], *out = argv[arg + 1];
	string track = argv[arg + 2], slotname = argv[arg + 3];
	int slot;
	if (slotname == "out")
		slot = 0;
	else if (slotname.size() == 1 && slotname[0] >= 'A' && slotname[0] <= 'J')
		slot = slotname[0] - 'A' + 1;
	else {
		fprintf(stderr, "Slot must be out or a letter A to J\n");
		return 2;
	}
	float db = 0;
	if (mute < 0) {
		char *end;
		db = strtof(argv[arg + 4], &end);
		if (*end || end == argv[arg + 4] || db < -80 || db > 12) {
			fprintf(stderr, "dB must be a number from -80 to +12\n");
			return 2;
		}
	}

	FILE *f = fopen(out, "rb");
	if (f) {
		fclose(f);
		fprintf(stderr, "%s exists; give a new file\n", out);
		return 2;
	}

	PTFFormat ptf;
	int ok = ptf.load(in, 48000);
	if (ok != 0) {
		fprintf(stderr, "Cannot read session %s (error %d)\n", in, ok);
		return 2;
	}

	/* Routes of one slot share it: change it once. */
	const PTFFormat::route_t *hit = NULL;
	for (vector<PTFFormat::route_t>::const_iterator r = ptf.routes().begin();
			r != ptf.routes().end(); ++r) {
		if (r->strip == track && r->slot == slot) {
			hit = &*r;
			break;
		}
	}
	if (!hit) {
		fprintf(stderr, "No assigned slot %s on %s\n", slotname.c_str(), track.c_str());
		return 1;
	}
	PTFFormat::route_t route = *hit;
	bool done = mute < 0 ? ptf.set_level(route, db) : ptf.set_route_muted(route, mute == 1);
	if (!done) {
		fprintf(stderr, "Cannot change slot %s of %s: it holds automation or is not as expected\n",
			slotname.c_str(), track.c_str());
		return 2;
	}
	if (ptf.save(out) != 0) {
		fprintf(stderr, "Cannot write %s\n", out);
		return 2;
	}
	if (mute < 0)
		printf("Set slot %s of %s to %+.1f dB\n", slotname.c_str(), track.c_str(), db);
	else
		printf("%s slot %s of %s\n", mute ? "Muted" : "Unmuted", slotname.c_str(), track.c_str());
	return 0;
}
