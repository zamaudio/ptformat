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
 * ptmarker: write a copy of a session with a marker added.
 *
 * Usage: ptmarker in.ptx out.ptx name at
 *
 * at is a timecode (hh:mm:ss:ff, from 00:00:00:00) or a sample count at
 * the session's rate. The marker gets the next free memory location
 * number. The session read is never written: out must be a new file.
 * The session must already hold a marker, which serves as the model.
 *
 * Exit status: 0 success, 2 usage, read or write error, or refused.
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
	if (argc != 5) {
		fprintf(stderr, "Usage: %s in.ptx out.ptx name at\n", argv[0]);
		return 2;
	}
	const char *in = argv[1], *out = argv[2];
	string name = argv[3];

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
	int64_t at = position(argv[4], rate, ptf.frameratenum(), ptf.framerateden(), ptf.dropframe());
	if (at < 0) {
		fprintf(stderr, "Not a timecode or a sample count: %s\n", argv[4]);
		return 2;
	}
	if (!ptf.add_marker(name, at)) {
		fprintf(stderr, "Cannot add the marker: the session holds no marker to copy, or it is not as expected\n");
		return 2;
	}
	if (ptf.save(out) != 0) {
		fprintf(stderr, "Cannot write %s\n", out);
		return 2;
	}
	uint16_t number = 0;
	for (vector<PTFFormat::marker_t>::const_iterator m = ptf.markers().begin(); m != ptf.markers().end(); ++m) {
		if (m->name == name && m->start == at)
			number = m->index;
	}
	printf("Added marker %u `%s` at %" PRId64 "\n", number, name.c_str(), at);
	return 0;
}
