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
#include <stdlib.h>
#include <cstdio>
#include <inttypes.h>
#include "ptformat/ptfformat.h"

int main(int argc, char** argv) {
	
	uint64_t i;
	PTFFormat ptf;

	if (argc < 2) {
		fprintf(stderr, "Need filename\n");
		exit(1);
	}

	if (ptf.unxor(std::string(argv[1])) == -1) {
		fprintf(stderr, "Can't decrypt pt session, still try to dump...\n");
	}

	if (ptf.ptfunxored) {
		for (i = 0; i < ptf.len; i++) {
			printf("%c", ptf.ptfunxored[i]);
		}
	}

	return 0;
}
