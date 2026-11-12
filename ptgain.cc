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
 * ptgain: write a copy of a session with the clip gain of a region set.
 *
 * Usage: ptgain in.ptx out.ptx region dB
 *
 * dB is a number, or -inf. Every placement of the region follows. The
 * session read is never written: out must be a new file. Only PT10 to
 * PT12 sessions.
 *
 * Exit status: 0 success, 1 no region with that name or several, 2
 * usage, read or write error, or a session the change does not support.
 */

#include "ptformat/ptformat.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace std;

int main (int argc, char **argv) {
	if (argc != 5) {
		fprintf(stderr, "Usage: %s in.ptx out.ptx region dB\n", argv[0]);
		return 2;
	}
	const char *in = argv[1], *out = argv[2];
	string from = argv[3];
	float db;
	if (string(argv[4]) == "-inf") {
		db = -HUGE_VALF;
	} else {
		char *end;
		db = strtof(argv[4], &end);
		if (*end || end == argv[4] || !std::isfinite(db)) {
			fprintf(stderr, "Not a gain in dB: %s\n", argv[4]);
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

	const PTFFormat::region_t *found = NULL;
	for (vector<PTFFormat::region_t>::const_iterator r = ptf.regions().begin();
			r != ptf.regions().end(); ++r) {
		if (r->name == from) {
			if (found) {
				fprintf(stderr, "Several regions are named %s\n", from.c_str());
				return 1;
			}
			found = &*r;
		}
	}
	if (!found) {
		fprintf(stderr, "No region named %s\n", from.c_str());
		return 1;
	}
	PTFFormat::region_t region = *found;
	if (!ptf.set_clip_gain(region, db)) {
		fprintf(stderr, "Cannot set the gain of %s in this session\n", from.c_str());
		return 2;
	}
	if (ptf.save(out) != 0) {
		fprintf(stderr, "Cannot write %s\n", out);
		return 2;
	}
	printf("Set the clip gain of %s to %s dB\n", from.c_str(), argv[4]);
	return 0;
}
