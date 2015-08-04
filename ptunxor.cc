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
	
	int count, i, li;
	PTFFormat ptf;

	if (argc < 2 || ptf.load(argv[1]) == -1) {
		fprintf(stderr, "Can't open ptf file, quit\n");
		exit(1);
	}
	
	count = 0;
	for (i = 0; i < 256; i++) {
		if (ptflutseenwild[i]) {
			count++;
		}
	}
	fprintf(stderr, "Total tables reversed: %d out of 128\n", count);

	switch (ptf.c0) {
	case 0x00:
		fprintf(stderr, "Success! easy one\n");
		for (i = 0; i < ptf.len; i++) {
			printf("%c", ptf.ptfunxored[i]);
		}
		break;
	case 0x80:
		fprintf(stderr, "Success! easy two\n");
		for (i = 0; i < ptf.len; i++) {
			printf("%c", ptf.ptfunxored[i]);
		}
		break;
	case 0x40:
	case 0xc0:
		li = ptf.c1;
		if (ptflutseenwild[li]) {
			fprintf(stderr, "Success! Found lookup table 0x%x\n", li);
			for (i = 0; i < ptf.len; i++) {
				printf("%c", ptf.ptfunxored[i]);
			}
		} else {
			fprintf(stderr, "Can't find lookup table for c1=0x%x\n", li);
			for (i = 0; i < ptf.len; i++) {
				printf("%02x ", ptf.ptfunxored[i]);
				if (i%16 == 15)
					printf("\n");
			}
		}
		break;
	default:
		fprintf(stderr, "Algorithm failed c[0] c[1]: %02x %02x\n", ptf.c0, ptf.c1);
	}
		
	return 0;
}
