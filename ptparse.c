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

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define NOTMAPPED 0
#define SAMPLES 1

void inplace_reverse(unsigned char * str)
{
  if (str)
  {
    unsigned char * end = str + strlen(str) - 1;

#   define XOR_SWAP(a,b) do\
    {\
      a ^= b;\
      b ^= a;\
      a ^= b;\
    } while (0)

    while (str < end)
    {
      XOR_SWAP(*str, *end);
      str++;
      end--;
    }
#   undef XOR_SWAP
  }
}

int main(int argc, char** argv) {
	FILE *fp;
	int i;
	int j;
	int k;
	int l;
	int len;
	int type;
	unsigned char *ptf;

	fp = fopen(argv[1], "rb");
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	
	fseek(fp, 0x0, SEEK_SET);
	if ((ptf = malloc(len*sizeof(unsigned char))) == 0) {
		printf("Out of memory\n");
		return 1;
	}
	
	fread(ptf, 1, len, fp);

	// Find end of wav file list
	k = 0;
	while (k < len) {
		if (		(ptf[k  ] == 0xff) &&
				(ptf[k+1] == 0xff) &&
				(ptf[k+2] == 0xff) &&
				(ptf[k+3] == 0xff)) {
			break;
		}	
		k++;
	}

	j = 0;
	l = 0;
	unsigned char wavname[256] = {0};
	for (i = k; i > 4; i--) {
		if (		(ptf[i  ] == 'W') &&
				(ptf[i-1] == 'A') &&
				(ptf[i-2] == 'V') &&
				(ptf[i-3] == 'E')) {
			j = i-4;
			l = 0;
			while (ptf[j] != '\0') {
				wavname[l] = ptf[j];
				l++;
				j--;
			}
			wavname[l] = 0;
			inplace_reverse(wavname);
			printf("%s\n", wavname);
		}
	}

	while (		(ptf[k  ] != 0x5a) &&
			(ptf[k+1] != 0x01) &&
			(ptf[k+2] != 0x00)) {
		k++;
	}
	for (i = k; i < len-70; i++) {
		if (		(ptf[i  ] == 0x5a) &&
				(ptf[i+1] == 0x0c) &&
				(ptf[i+2] == 0x00)) {
			unsigned char name[65] = {0};
			for (j = i + 13; j < i + 13 + 64; j++) {
				name[j-(i+13)] = ptf[j];
				if (ptf[j] == 0x00) {
					type = SAMPLES;
					name[j-(i+13)] = 0x00;
					break;
				}
				if (ptf[j] == 0x01) {
					type = NOTMAPPED;
					name[j-(i+13)] = 0x00;
					break;
				}

			}
			int samplebytes1 = (ptf[j+3] & 0xf);
			unsigned char samplebytehigh = ptf[j+16];

			if ((samplebytes1 == 3) && (samplebytehigh == 0xfe)) {
				samplebytes1 = 4;
			}
			//int samplebytes2 = (ptf[j+1] & 0xf0) >> 4;
			//printf("sbh=0x%x\n", samplebytehigh);
			int samplebytes = samplebytes1;
			if (type == SAMPLES) {
				uint32_t offset = 0;
				switch (samplebytes) {
				case 4:
					offset |= (uint32_t)(ptf[j+11] << 24);
				case 3:
					offset |= (uint32_t)(ptf[j+10] << 16);
				case 2:
					offset |= (uint32_t)(ptf[j+9] << 8);
				case 1:
					offset |= (uint32_t)(ptf[j+8]);
				default:
					break;
				}
				//printf("%s sampleoffset = %08x = %d\n", name, offset, offset);
				printf("%s\t%u\n", name, offset);
			}
		}
	}

	fclose(fp);
	free(ptf);
	return 0;
}
