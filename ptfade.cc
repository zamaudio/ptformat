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
 * ptfade: write a copy of a session with a fade-in or fade-out added to
 * the regions of a track that start at one place.
 *
 * Usage: ptfade in.ptx out.ptx track start in|out length [linear|power]
 *
 * start is where the regions start, and length how long the fade is,
 * both as timecode (hh:mm:ss:ff, or hh:mm:ss;ff for drop frame, at the
 * session's frame rate) or samples at the session's rate. Every
 * placement on the track that starts at start gets the fade, so all
 * channels of a multichannel track get it. The shape is linear unless
 * power (equal power) is given. The session read is never written: out
 * must be a new file. A fade longer than the region, on an edge that
 * already has one, on a region another one touches, or in a session
 * with no fade to copy from, is refused.
 *
 * Exit status: 0 success, 1 nothing starts at start on the track, 2
 * usage, read or write error, or a fade refused.
 */

#include "ptformat/ptformat.h"
#include <inttypes.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace std;

/* Samples at rate for a timecode or a sample count; -1 when neither. */
static int64_t
position(const char *s, int64_t rate, uint32_t num, uint32_t den, bool drop)
{
	unsigned h, m, sec, f;
	char sep;
	if (sscanf(s, "%u:%u:%u%c%u", &h, &m, &sec, &sep, &f) == 5 && (sep == ':' || sep == ';')) {
		if (!num || !den || m > 59 || sec > 59)
			return -1;
		int64_t base = (num + den / 2) / den;
		if ((int64_t)f >= base)
			return -1;
		int64_t minutes = 60 * (int64_t)h + m;
		int64_t frames = (60 * minutes + sec) * base + f;
		if (drop) {
			int64_t dropped = 2 * (base / 30);
			frames -= dropped * (minutes - minutes / 10);
		}
		/* Round to the nearest sample. */
		return (2 * frames * rate * den + num) / (2 * (int64_t)num);
	}
	char *end;
	long long v = strtoll(s, &end, 10);
	if (*end || end == s || v < 0)
		return -1;
	return v;
}

int main (int argc, char **argv) {
	if (argc != 7 && argc != 8) {
		fprintf(stderr, "Usage: %s in.ptx out.ptx track start in|out length [linear|power]\n", argv[0]);
		return 2;
	}
	const char *in = argv[1], *out = argv[2];
	string track = argv[3], kind = argv[5];
	string shape = argc == 8 ? argv[7] : "linear";
	if ((kind != "in" && kind != "out") || (shape != "linear" && shape != "power")) {
		fprintf(stderr, "Usage: %s in.ptx out.ptx track start in|out length [linear|power]\n", argv[0]);
		return 2;
	}

	FILE *fp = fopen(out, "rb");
	if (fp) {
		fclose(fp);
		fprintf(stderr, "%s exists; give a new file\n", out);
		return 2;
	}

	/* Read once to learn the session's rate, then at that rate, so
	 * positions are the session's own samples. */
	PTFFormat probe;
	if (probe.load(in, 48000) != 0) {
		fprintf(stderr, "Cannot read session %s\n", in);
		return 2;
	}
	int64_t rate = probe.sessionrate();
	PTFFormat ptf;
	int ok = ptf.load(in, rate);
	if (ok != 0) {
		fprintf(stderr, "Cannot read session %s (error %d)\n", in, ok);
		return 2;
	}
	int64_t start = position(argv[4], rate, ptf.frameratenum(), ptf.framerateden(), ptf.dropframe());
	/* A length is a duration: no drop-frame numbering. */
	int64_t length = position(argv[6], rate, ptf.frameratenum(), ptf.framerateden(), false);
	if (start < 0 || length <= 0) {
		fprintf(stderr, "Not a timecode or a sample count: %s\n", start < 0 ? argv[4] : argv[6]);
		return 2;
	}

	/* The entries of the placements found first: each fade parses the
	 * session again, so look them up again by position each time. */
	vector<string> regions;
	for (vector<PTFFormat::track_t>::const_iterator t = ptf.tracks().begin();
			t != ptf.tracks().end(); ++t) {
		if (t->name == track && t->reg.startpos == start)
			regions.push_back(t->reg.name);
	}
	if (regions.empty()) {
		fprintf(stderr, "Nothing starts at %s on %s\n", argv[4], track.c_str());
		return 1;
	}
	for (vector<string>::const_iterator r = regions.begin(); r != regions.end(); ++r) {
		const PTFFormat::track_t *hit = NULL;
		for (vector<PTFFormat::track_t>::const_iterator t = ptf.tracks().begin();
				t != ptf.tracks().end(); ++t) {
			if (t->name == track && t->reg.startpos == start && t->reg.name == *r)
				hit = &*t;
		}
		if (!hit) {
			fprintf(stderr, "Lost %s on %s\n", r->c_str(), track.c_str());
			return 2;
		}
		PTFFormat::track_t placement = *hit;
		if (!ptf.add_fade(placement, kind == "in", length, shape == "power" ? 1 : 2)) {
			fprintf(stderr, "Cannot add the fade to %s on %s\n", r->c_str(), track.c_str());
			return 2;
		}
	}
	if (ptf.save(out) != 0) {
		fprintf(stderr, "Cannot write %s\n", out);
		return 2;
	}
	printf("Added a fade-%s to %zu placement%s on %s\n", kind.c_str(), regions.size(),
		regions.size() == 1 ? "" : "s", track.c_str());
	return 0;
}
