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

#include "ptformat/ptformat.h"
#include <inttypes.h> // PRIx
#include <cstdio>
#include <stdlib.h>

using namespace std;
using std::string;

int main (int argc, char **argv) {
	PTFFormat ptf;
	int ok;

	if (argc == 1) {
		printf("No ptf file specified, quit\n");
		exit(0);
	}

	ok = ptf.load(argv[1], 48000);

	switch (ok) {
	default:
	case -1:
		printf("Cannot open ptf, quit\n");
		exit(-1);
		break;
	case 0:
		printf("#!/bin/bash\nset -e\nmkdir \"Audio Files\"\n");
		for (vector<PTFFormat::wav_t>::const_iterator a = ptf.audiofiles().begin();
					a != ptf.audiofiles().end(); ++a) {
			if (!a->length) {
				printf("# unknown length : %s\n", a->filename.c_str());
			} else {
				printf("sox --no-clobber -S -n -r %" PRId64 " -c 1 -b 16 \"Audio Files\"/\"%s\" synth %" PRId64 "s sine 1000 gain -18\n",
					ptf.sessionrate(),
					a->filename.c_str(),
					a->length
				);
			}
		}
		break;
	}
	exit(0);
}
