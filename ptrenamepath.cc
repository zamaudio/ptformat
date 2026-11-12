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
 * ptrenamepath: write a copy of a session with an I/O Setup path
 * renamed.
 *
 * Usage: ptrenamepath in.ptx out.ptx in|out path new-name
 *
 * "out" names an output or bus path, "in" an input path. Sub-paths keep
 * their names. The session read is never written: out must be a new
 * file.
 *
 * Exit status: 0 success, 1 no such path, 2 usage, read or write error,
 * or refused.
 */

#include "ptformat/ptformat.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace std;

int main (int argc, char **argv) {
	if (argc != 6 || (strcmp(argv[3], "in") != 0 && strcmp(argv[3], "out") != 0)) {
		fprintf(stderr, "Usage: %s in.ptx out.ptx in|out path new-name\n", argv[0]);
		return 2;
	}
	const char *in = argv[1], *out = argv[2];
	uint16_t list = strcmp(argv[3], "out") == 0 ? 0x2603 : 0x1022;
	string path = argv[4], name = argv[5];

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
	for (vector<PTFFormat::iopath_t>::const_iterator p = ptf.iopaths().begin(); p != ptf.iopaths().end(); ++p) {
		if (p->list == list && p->name == path) {
			if (!id.empty() && id != p->id) {
				fprintf(stderr, "More than one path is named %s: not changed\n", path.c_str());
				return 2;
			}
			id = p->id;
		}
	}
	if (id.empty()) {
		fprintf(stderr, "No %s path %s\n", argv[3], path.c_str());
		return 1;
	}
	if (!ptf.rename_iopath(id, name)) {
		fprintf(stderr, "Cannot rename %s: another path has the name, or the session is not as expected\n",
			path.c_str());
		return 2;
	}
	if (ptf.save(out) != 0) {
		fprintf(stderr, "Cannot write %s\n", out);
		return 2;
	}
	printf("Renamed path %s to %s\n", path.c_str(), name.c_str());
	return 0;
}
