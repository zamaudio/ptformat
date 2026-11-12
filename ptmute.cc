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
 * ptmute: write a copy of a session with the placements of a region on
 * a track muted or unmuted.
 *
 * Usage: ptmute [--unmute] in.ptx out.ptx track region
 *
 * Every placement of the region named on the track named changes. The
 * session read is never written: out must be a new file. Only the mute
 * byte of each placement changes; the rest of the copy is identical.
 *
 * Exit status: 0 success, 1 nothing matched, 2 usage, read or write
 * error.
 */

#include "ptformat/ptformat.h"
#include <inttypes.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace std;

int main (int argc, char **argv) {
	bool mute = true;
	int arg = 1;
	if (arg < argc && strcmp(argv[arg], "--unmute") == 0) {
		mute = false;
		arg++;
	}
	if (argc - arg != 4) {
		fprintf(stderr, "Usage: %s [--unmute] in.pt{s,5,f,x} out.pt{s,5,f,x} track region\n", argv[0]);
		return 2;
	}
	const char *in = argv[arg], *out = argv[arg + 1];
	string track = argv[arg + 2], region = argv[arg + 3];

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

	/* Copy the matches first: set_muted() updates tracks(). */
	vector<PTFFormat::track_t> hits;
	for (vector<PTFFormat::track_t>::const_iterator t = ptf.tracks().begin();
			t != ptf.tracks().end(); ++t) {
		if (t->name == track && t->reg.name == region)
			hits.push_back(*t);
	}
	if (hits.empty()) {
		fprintf(stderr, "No placement of %s on %s\n", region.c_str(), track.c_str());
		return 1;
	}
	for (vector<PTFFormat::track_t>::const_iterator t = hits.begin(); t != hits.end(); ++t) {
		if (!ptf.set_muted(*t, mute)) {
			fprintf(stderr, "Cannot change the placement at %" PRId64 "\n", t->reg.startpos);
			return 2;
		}
	}
	if (ptf.save(out) != 0) {
		fprintf(stderr, "Cannot write %s\n", out);
		return 2;
	}
	printf("%s %zu placement%s of %s on %s\n", mute ? "Muted" : "Unmuted",
		hits.size(), hits.size() == 1 ? "" : "s", region.c_str(), track.c_str());
	return 0;
}
