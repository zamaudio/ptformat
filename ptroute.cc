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
 * ptroute: write a copy of a session with the output or a send of a
 * track sent to another output path or bus.
 *
 * Usage: ptroute in.ptx out.ptx track out|A-J destination
 *
 * The destination must be an output path or bus of the session with the
 * same number of channels as the current one, and some track of the
 * session must already send to it. The session read is never written:
 * out must be a new file.
 *
 * Exit status: 0 success, 1 no such track or slot, 2 usage, read or
 * write error, or refused.
 */

#include "ptformat/ptformat.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace std;

int main (int argc, char **argv) {
	if (argc != 6) {
		fprintf(stderr, "Usage: %s in.ptx out.ptx track out|A-J destination\n", argv[0]);
		return 2;
	}
	const char *in = argv[1], *out = argv[2];
	string track = argv[3], slotname = argv[4], dest = argv[5];
	int slot;
	if (slotname == "out")
		slot = 0;
	else if (slotname.size() == 1 && slotname[0] >= 'A' && slotname[0] <= 'J')
		slot = slotname[0] - 'A' + 1;
	else {
		fprintf(stderr, "Slot must be out or a letter A to J\n");
		return 2;
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
	/* A slot can hold several routes: only one record is changed. */
	const PTFFormat::route_t *hit = NULL;
	for (vector<PTFFormat::route_t>::const_iterator r = ptf.routes().begin();
			r != ptf.routes().end(); ++r) {
		if (r->strip == track && r->slot == slot) {
			if (hit) {
				fprintf(stderr, "Slot %s of %s holds more than one route: not changed\n",
					slotname.c_str(), track.c_str());
				return 2;
			}
			hit = &*r;
		}
	}
	if (!hit) {
		fprintf(stderr, "No assigned slot %s on %s\n", slotname.c_str(), track.c_str());
		return 1;
	}
	string old = hit->destination;
	PTFFormat::route_t route = *hit;
	if (!ptf.set_route_destination(route, dest)) {
		fprintf(stderr, "Cannot send slot %s of %s to %s: it must be another output path or bus with the "
			"same channels that another track already sends to\n", slotname.c_str(), track.c_str(), dest.c_str());
		return 2;
	}
	if (ptf.save(out) != 0) {
		fprintf(stderr, "Cannot write %s\n", out);
		return 2;
	}
	printf("Slot %s of %s now goes to %s (was %s)\n", slotname.c_str(), track.c_str(), dest.c_str(), old.c_str());
	return 0;
}
