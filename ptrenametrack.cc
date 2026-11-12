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
 * ptrenametrack: write a copy of a session with a track renamed.
 *
 * Usage: ptrenametrack in.ptx out.ptx track new-name
 *
 * The session read is never written: out must be a new file. The
 * playlist of an audio track keeps its name.
 *
 * Exit status: 0 success, 1 no such track, 2 usage, read or write
 * error, or refused.
 */

#include "ptformat/ptformat.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace std;

int main (int argc, char **argv) {
	if (argc != 5) {
		fprintf(stderr, "Usage: %s in.ptx out.ptx track new-name\n", argv[0]);
		return 2;
	}
	const char *in = argv[1], *out = argv[2];
	string track = argv[3], name = argv[4];

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
	string id;
	for (vector<PTFFormat::strip_t>::const_iterator s = ptf.strips().begin(); s != ptf.strips().end(); ++s) {
		if (s->name == track)
			id = s->id;
	}
	if (id.empty()) {
		fprintf(stderr, "No track %s\n", track.c_str());
		return 1;
	}
	if (!ptf.rename_track(id, name)) {
		fprintf(stderr, "Cannot rename %s: another track has the name, or the session is not as expected\n",
			track.c_str());
		return 2;
	}
	if (ptf.save(out) != 0) {
		fprintf(stderr, "Cannot write %s\n", out);
		return 2;
	}
	printf("Renamed track %s to %s\n", track.c_str(), name.c_str());
	return 0;
}
