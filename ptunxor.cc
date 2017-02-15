/*
 * Copyright (C) 2015  Damien Zammit <damien@zamaudio.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <cstdio>
#include <inttypes.h>
#include "ptfformat.h"

int main(int argc, char** argv) {
	
	uint64_t i;
	int li;
	PTFFormat ptf;

	if (argc < 2 || ptf.load(argv[1], 48000) == -1) {
		fprintf(stderr, "Can't open ptf file, quit\n");
		exit(1);
	}

	for (i = 0; i < ptf.len; i++) {
		printf("%c", ptf.ptfunxored[i]);
	}

	return 0;
}
