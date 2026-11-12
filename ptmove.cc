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
 * ptmove: write a copy of a session with the regions of a track that
 * start at one place moved to another.
 *
 * Usage: ptmove in.ptx out.ptx track from to
 *
 * from and to are timecode (hh:mm:ss:ff, or hh:mm:ss;ff for drop frame,
 * counted from 00:00:00:00 at the session's frame rate) or samples at
 * the session's rate. Every placement on the track that starts at from
 * moves, so all channels of a multichannel track move together. The
 * session read is never written: out must be a new file. A move that
 * would overlap another region, or that touches or crosses a fade, is
 * refused.
 *
 * Exit status: 0 success, 1 nothing starts at from on the track, 2
 * usage, read or write error, or a move refused.
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
	if (argc != 6) {
		fprintf(stderr, "Usage: %s in.ptx out.ptx track from to\n", argv[0]);
		return 2;
	}
	const char *in = argv[1], *out = argv[2];
	string track = argv[3];

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
	int64_t from = position(argv[4], rate, ptf.frameratenum(), ptf.framerateden(), ptf.dropframe());
	int64_t to = position(argv[5], rate, ptf.frameratenum(), ptf.framerateden(), ptf.dropframe());
	if (from < 0 || to < 0) {
		fprintf(stderr, "Not a timecode or a sample count: %s\n", from < 0 ? argv[4] : argv[5]);
		return 2;
	}

	/* Move one placement at a time: each move parses the session again. */
	size_t moved = 0;
	for (;;) {
		const PTFFormat::track_t *hit = NULL;
		for (vector<PTFFormat::track_t>::const_iterator t = ptf.tracks().begin();
				t != ptf.tracks().end(); ++t) {
			if (t->name == track && t->reg.startpos == from) {
				hit = &*t;
				break;
			}
		}
		if (!hit)
			break;
		PTFFormat::track_t placement = *hit;
		if (from == to || !ptf.set_position(placement, to)) {
			fprintf(stderr, "Cannot move %s on %s: it would overlap another region or a fade\n",
				placement.reg.name.c_str(), track.c_str());
			return 2;
		}
		moved++;
	}
	if (!moved) {
		fprintf(stderr, "Nothing starts at %s on %s\n", argv[4], track.c_str());
		return 1;
	}
	if (ptf.save(out) != 0) {
		fprintf(stderr, "Cannot write %s\n", out);
		return 2;
	}
	printf("Moved %zu placement%s on %s from %s to %s\n", moved, moved == 1 ? "" : "s",
		track.c_str(), argv[4], argv[5]);
	return 0;
}
