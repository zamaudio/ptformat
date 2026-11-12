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
 * ptinput: write a copy of a session with the input of a track set to
 * another input path or bus, or to no input.
 *
 * Usage: ptinput in.ptx out.ptx track input
 *
 * input is a path of the I/O Setup input page or a bus, with as many
 * channels as the track; an empty input ("") leaves the track with no
 * input. The session read is never written: out must be a new file.
 *
 * Exit status: 0 success, 1 no such track or input, 2 usage, read or
 * write error, or refused.
 */

#include "ptformat/ptformat.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace std;

int main (int argc, char **argv) {
	if (argc != 5) {
		fprintf(stderr, "Usage: %s in.ptx out.ptx track input\n", argv[0]);
		return 2;
	}
	const char *in = argv[1], *out = argv[2];
	string track = argv[3], input = argv[4];

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
	const PTFFormat::strip_t *hit = NULL;
	for (vector<PTFFormat::strip_t>::const_iterator s = ptf.strips().begin(); s != ptf.strips().end(); ++s) {
		if (s->name == track) {
			if (hit) {
				fprintf(stderr, "More than one track is named %s: not changed\n", track.c_str());
				return 2;
			}
			hit = &*s;
		}
	}
	if (!hit) {
		fprintf(stderr, "No track %s\n", track.c_str());
		return 1;
	}
	if (!input.empty()) {
		bool known = false;
		for (vector<PTFFormat::iopath_t>::const_iterator p = ptf.iopaths().begin(); p != ptf.iopaths().end(); ++p) {
			if (p->list == 0x1022 && p->parent.empty() && p->name == input)
				known = true;
		}
		if (!known) {
			fprintf(stderr, "No input path or bus %s\n", input.c_str());
			return 1;
		}
	}
	string id = hit->id, old = hit->input;
	if (!ptf.set_input(id, input)) {
		fprintf(stderr, "Cannot set the input of %s to %s: it must be another input with the track's "
			"channels, or the session is not as expected\n", track.c_str(), input.empty() ? "none" : input.c_str());
		return 2;
	}
	if (ptf.save(out) != 0) {
		fprintf(stderr, "Cannot write %s\n", out);
		return 2;
	}
	printf("Input of %s is now %s (was %s)\n", track.c_str(),
		input.empty() ? "none" : input.c_str(), old.empty() ? "none" : old.c_str());
	return 0;
}
