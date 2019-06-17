/*
 * libptformat - a library to read ProTools sessions
 *
 * Copyright (C) 2015  Damien Zammit
 * Copyright (C) 2015  Robin Gareus
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

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <assert.h>

#include <glib/gstdio.h>

#include "ptformat/ptfformat.h"

#if 0
#define verbose_printf(...) printf(__VA_ARGS__)
#else
#define verbose_printf(...)
#endif

using namespace std;

static bool wavidx_compare(PTFFormat::wav_t& w1, PTFFormat::wav_t& w2) {
	return w1.index < w2.index;
}

static bool wavname_compare(PTFFormat::wav_t& w1, PTFFormat::wav_t& w2) {
	return (strcasecmp(w1.filename.c_str(), w2.filename.c_str()) < 0);
}

static bool regidx_compare(PTFFormat::region_t& r1, PTFFormat::region_t& r2) {
	return r1.index < r2.index;
}

static bool regname_compare(PTFFormat::region_t& r1, PTFFormat::region_t& r2) {
	return (strcasecmp(r1.name.c_str(), r2.name.c_str()) < 0);
}

static void
hexdump(uint8_t *data, int length, int level)
{
	int i,j,k,end,step=16;

	for (i = 0; i < length; i += step) {
		end = i + step;
		if (end > length) end = length;
		for (k = 0; k < level; k++)
			printf("    ");
		for (j = i; j < end; j++) {
			printf("%02X ", data[j]);
		}
		for (j = i; j < end; j++) {
			if (data[j] < 128 && data[j] > 32)
				printf("%c", data[j]);
			else
				printf(".");
		}
		printf("\n");
	}
}

PTFFormat::PTFFormat() : version(0), product(NULL), ptfunxored(NULL) {
}

PTFFormat::~PTFFormat() {
	cleanup();
}

std::string
PTFFormat::get_content_description(uint16_t ctype) {
	switch(ctype) {
	case 0x0030:
		return std::string("INFO product and version");
	case 0x1001:
		return std::string("WAV samplerate, size");
	case 0x1003:
		return std::string("WAV metadata");
	case 0x1004:
		return std::string("WAV list full");
	case 0x1007:
		return std::string("region name, number");
	case 0x1008:
		return std::string("AUDIO region name, number (v5)");
	case 0x100b:
		return std::string("AUDIO region list (v5)");
	case 0x100f:
		return std::string("AUDIO region->track entry");
	case 0x1011:
		return std::string("AUDIO region->track map entries");
	case 0x1012:
		return std::string("AUDIO region->track full map");
	case 0x1014:
		return std::string("AUDIO track name, number");
	case 0x1015:
		return std::string("AUDIO tracks");
	case 0x1017:
		return std::string("PLUGIN entry");
	case 0x1018:
		return std::string("PLUGIN full list");
	case 0x1021:
		return std::string("I/O channel entry");
	case 0x1022:
		return std::string("I/O channel list");
	case 0x1028:
		return std::string("INFO sample rate");
	case 0x103a:
		return std::string("WAV names");
	case 0x104f:
		return std::string("AUDIO region->track subentry (v8)");
	case 0x1050:
		return std::string("AUDIO region->track entry (v8)");
	case 0x1052:
		return std::string("AUDIO region->track map entries (v8)");
	case 0x1054:
		return std::string("AUDIO region->track full map (v8)");
	case 0x1056:
		return std::string("MIDI region->track entry");
	case 0x1057:
		return std::string("MIDI region->track map entries");
	case 0x1058:
		return std::string("MIDI region->track full map");
	case 0x2000:
		return std::string("MIDI events block");
	case 0x2001:
		return std::string("MIDI region name, number (v5)");
	case 0x2002:
		return std::string("MIDI regions map (v5)");
	case 0x2067:
		return std::string("INFO path of session");
	case 0x2511:
		return std::string("Snaps block");
	case 0x2519:
		return std::string("MIDI track full list");
	case 0x251a:
		return std::string("MIDI track name, number");
	case 0x2523:
		return std::string("COMPOUND region element");
	case 0x2602:
		return std::string("I/O route");
	case 0x2603:
		return std::string("I/O routing table");
	case 0x2628:
		return std::string("COMPOUND region group");
	case 0x2629:
		return std::string("AUDIO region name, number (v10)");
	case 0x262a:
		return std::string("AUDIO region list (v10)");
	case 0x262c:
		return std::string("COMPOUND region full map");
	case 0x2633:
		return std::string("MIDI regions name, number (v10)");
	case 0x2634:
		return std::string("MIDI regions map (v10)");
	case 0x271a:
		return std::string("MARKER list");
	default:
		return std::string("UNKNOWN content type");
	}
}

uint16_t
PTFFormat::u_endian_read2(unsigned char *buf, bool bigendian)
{
	if (bigendian) {
		return ((uint16_t)(buf[0]) << 8) | (uint16_t)(buf[1]);
	} else {
		return ((uint16_t)(buf[1]) << 8) | (uint16_t)(buf[0]);
	}
}

uint32_t
PTFFormat::u_endian_read3(unsigned char *buf, bool bigendian)
{
	if (bigendian) {
		return ((uint32_t)(buf[0]) << 16) |
			((uint32_t)(buf[1]) << 8) |
			(uint32_t)(buf[2]);
	} else {
		return ((uint32_t)(buf[2]) << 16) |
			((uint32_t)(buf[1]) << 8) |
			(uint32_t)(buf[0]);
	}
}

uint32_t
PTFFormat::u_endian_read4(unsigned char *buf, bool bigendian)
{
	if (bigendian) {
		return ((uint32_t)(buf[0]) << 24) |
			((uint32_t)(buf[1]) << 16) |
			((uint32_t)(buf[2]) << 8) |
			(uint32_t)(buf[3]);
	} else {
		return ((uint32_t)(buf[3]) << 24) |
			((uint32_t)(buf[2]) << 16) |
			((uint32_t)(buf[1]) << 8) |
			(uint32_t)(buf[0]);
	}
}

uint64_t
PTFFormat::u_endian_read5(unsigned char *buf, bool bigendian)
{
	if (bigendian) {
		return ((uint64_t)(buf[0]) << 32) |
			((uint64_t)(buf[1]) << 24) |
			((uint64_t)(buf[2]) << 16) |
			((uint64_t)(buf[3]) << 8) |
			(uint64_t)(buf[4]);
	} else {
		return ((uint64_t)(buf[4]) << 32) |
			((uint64_t)(buf[3]) << 24) |
			((uint64_t)(buf[2]) << 16) |
			((uint64_t)(buf[1]) << 8) |
			(uint64_t)(buf[0]);
	}
}

uint64_t
PTFFormat::u_endian_read8(unsigned char *buf, bool bigendian)
{
	if (bigendian) {
		return ((uint64_t)(buf[0]) << 56) |
			((uint64_t)(buf[1]) << 48) |
			((uint64_t)(buf[2]) << 40) |
			((uint64_t)(buf[3]) << 32) |
			((uint64_t)(buf[4]) << 24) |
			((uint64_t)(buf[5]) << 16) |
			((uint64_t)(buf[6]) << 8) |
			(uint64_t)(buf[7]);
	} else {
		return ((uint64_t)(buf[7]) << 56) |
			((uint64_t)(buf[6]) << 48) |
			((uint64_t)(buf[5]) << 40) |
			((uint64_t)(buf[4]) << 32) |
			((uint64_t)(buf[3]) << 24) |
			((uint64_t)(buf[2]) << 16) |
			((uint64_t)(buf[1]) << 8) |
			(uint64_t)(buf[0]);
	}
}

void
PTFFormat::cleanup(void) {
	if (ptfunxored) {
		free(ptfunxored);
		ptfunxored = NULL;
	}
	audiofiles.clear();
	regions.clear();
	midiregions.clear();
	compounds.clear();
	tracks.clear();
	miditracks.clear();
	version = 0;
	product = NULL;
}

int64_t
PTFFormat::foundat(unsigned char *haystack, uint64_t n, const char *needle) {
	int64_t found = 0;
	uint64_t i, j, needle_n;
	needle_n = strlen(needle);

	for (i = 0; i < n; i++) {
		found = i;
		for (j = 0; j < needle_n; j++) {
			if (haystack[i+j] != needle[j]) {
				found = -1;
				break;
			}
		}
		if (found > 0)
			return found;
	}
	return -1;
}

bool
PTFFormat::jumpto(uint32_t *currpos, unsigned char *buf, const uint32_t maxoffset, const unsigned char *needle, const uint32_t needlelen) {
	uint64_t i;
	uint64_t k = *currpos;
	while (k + needlelen < maxoffset) {
		bool foundall = true;
		for (i = 0; i < needlelen; i++) {
			if (buf[k+i] != needle[i]) {
				foundall = false;
				break;
			}
		}
		if (foundall) {
			*currpos = k;
			return true;
		}
		k++;
	}
	return false;
}

bool
PTFFormat::jumpback(uint32_t *currpos, unsigned char *buf, const uint32_t maxoffset, const unsigned char *needle, const uint32_t needlelen) {
	uint64_t i;
	uint64_t k = *currpos;
	while (k > 0 && k + needlelen < maxoffset) {
		bool foundall = true;
		for (i = 0; i < needlelen; i++) {
			if (buf[k+i] != needle[i]) {
				foundall = false;
				break;
			}
		}
		if (foundall) {
			*currpos = k;
			return true;
		}
		k--;
	}
	return false;
}

bool
PTFFormat::foundin(std::string haystack, std::string needle) {
	size_t found = haystack.find(needle);
	if (found != std::string::npos) {
		return true;
	} else {
		return false;
	}
}

/* Return values:	0            success
			-1           could not decrypt pt session
*/
int
PTFFormat::unxor(std::string path) {
	FILE *fp;
	unsigned char xxor[256];
	unsigned char ct;
	uint64_t i;
	uint8_t xor_type;
	uint8_t xor_value;
	uint8_t xor_delta;
	uint16_t xor_len;

	if (! (fp = g_fopen(path.c_str(), "rb"))) {
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	if (len < 0x14) {
		fclose(fp);
		return -1;
	}

	if (! (ptfunxored = (unsigned char*) malloc(len * sizeof(unsigned char)))) {
		/* Silently fail -- out of memory*/
		fclose(fp);
		ptfunxored = 0;
		return -1;
	}

	/* The first 20 bytes are always unencrypted */
	fseek(fp, 0x00, SEEK_SET);
	i = fread(ptfunxored, 1, 0x14, fp);
	if (i < 0x14) {
		fclose(fp);
		return -1;
	}

	xor_type = ptfunxored[0x12];
	xor_value = ptfunxored[0x13];
	xor_len = 256;

	// xor_type 0x01 = ProTools 5, 6, 7, 8 and 9
	// xor_type 0x05 = ProTools 10, 11, 12
	switch(xor_type) {
	case 0x01:
		xor_delta = gen_xor_delta(xor_value, 53, false);
		break;
	case 0x05:
		xor_delta = gen_xor_delta(xor_value, 11, true);
		break;
	default:
		fclose(fp);
		return -1;
	}

	/* Generate the xor_key */
	for (i=0; i < xor_len; i++)
		xxor[i] = (i * xor_delta) & 0xff;

	/* hexdump(xxor, xor_len); */

	/* Read file and decrypt rest of file */
	i = 0x14;
	fseek(fp, i, SEEK_SET);
	while (fread(&ct, 1, 1, fp) != 0) {
		uint8_t xor_index = (xor_type == 0x01) ? i & 0xff : (i >> 12) & 0xff;
		ptfunxored[i++] = ct ^ xxor[xor_index];
	}
	fclose(fp);
	return 0;
}

/* Return values:	0            success
			-1           could not parse pt session
*/
int
PTFFormat::load(std::string ptf, int64_t targetsr) {
	cleanup();
	path = ptf;

	if (unxor(path))
		return -1;

	if (parse_version())
		return -1;

	if (version < 5 || version > 12)
		return -1;

	targetrate = targetsr;

	if (parse())
		return -1;

	return 0;
}

bool
PTFFormat::parse_version() {
	uint32_t seg_len,str_len;
	uint8_t *data = ptfunxored + 0x14;
	uintptr_t data_end = ((uintptr_t)ptfunxored) + 0x100;
	uint8_t seg_type;
	bool success = false;

	if (ptfunxored[0] != '\x03' && foundat(ptfunxored, 0x100, BITCODE) != 1) {
		return false;
	}

	while( ((uintptr_t)data < data_end) && (success == false) ) {

		if (data[0] != 0x5a) {
			success = false;
			break;
		}

		seg_type = data[1];
		/* Skip segment header */
		data += 3;
		if (data[0] == 0 && data[1] == 0) {
			/* BE */
			is_bigendian = true;
		} else {
			/* LE */
			is_bigendian = false;
		}
		seg_len = u_endian_read4(&data[0], is_bigendian);

		/* Skip seg_len */
		data += 4;
		if (!(seg_type == 0x04 || seg_type == 0x03) || data[0] != 0x03) {
			/* Go to next segment */
			data += seg_len;
			continue;
		}
		/* Skip 0x03 0x00 0x00 */
		data += 3;
		seg_len -= 3;
		str_len = (*(uint8_t *)data);
		if (! (product = (uint8_t *)malloc((str_len+1) * sizeof(uint8_t)))) {
			success = false;
			break;
		}

		/* Skip str_len */
		data += 4;
		seg_len -= 4;

		memcpy(product, data, str_len);
		product[str_len] = 0;
		data += str_len;
		seg_len -= str_len;

		/* Skip 0x03 0x00 0x00 0x00 */
		data += 4;
		seg_len -= 4;

		version = data[0];
		if (version == 0) {
			version = data[3];
		}
		data += seg_len;
		success = true;
	}

	/* If the above does not work, try other heuristics */
	if ((uintptr_t)data >= data_end - seg_len) {
		version = ptfunxored[0x40];
		if (version == 0) {
			version = ptfunxored[0x3d];
		}
		if (version == 0) {
			version = ptfunxored[0x3a] + 2;
		}
		if (version != 0) {
			success = true;
		}
	}
	return (!success);
}

uint8_t
PTFFormat::gen_xor_delta(uint8_t xor_value, uint8_t mul, bool negative) {
	uint16_t i;
	for (i = 0; i < 256; i++) {
		if (((i * mul) & 0xff) == xor_value) {
				return (negative) ? i * (-1) : i;
		}
	}
	// Should not occur
	return 0;
}

void
PTFFormat::setrates(void) {
	ratefactor = 1.f;
	if (sessionrate != 0) {
		ratefactor = (float)targetrate / sessionrate;
	}
}

bool
PTFFormat::parse_block_at(uint32_t pos, struct block_t *block, int level) {
	struct block_t b;
	int childjump;
	uint32_t i;

	if (pos + 7 > len)
		return false;
	if (level > 10)
		return false;
	if (ptfunxored[pos] != ZMARK)
		return false;

	b.zmark = ZMARK;
	b.block_type = u_endian_read2(&ptfunxored[pos+1], is_bigendian);
	b.block_size = u_endian_read4(&ptfunxored[pos+3], is_bigendian);
	b.content_type = u_endian_read2(&ptfunxored[pos+7], is_bigendian);
	b.offset = pos + 7;

	if (b.block_size + b.offset > len)
		return false;
	if (b.block_type & 0xff00)
		return false;

	block->zmark = b.zmark;
	block->block_type = b.block_type;
	block->block_size = b.block_size;
	block->content_type = b.content_type;
	block->offset = b.offset;

	for (i = 1; (i < block->block_size) && (pos + i + childjump < len); i += childjump ? childjump : 1) {
		int p = pos + i;
		struct block_t bchild;
		childjump = 0;
		if (parse_block_at(p, &bchild, level+1)) {
			block->child.push_back(bchild);
			childjump = bchild.block_size + 7;
		}
	}
	return true;
}

void
PTFFormat::dump_block(struct block_t& b, int level)
{
	int i;

	for (i = 0; i < level; i++) {
		printf("    ");
	}
	printf("%s(0x%04x)\n", get_content_description(b.content_type).c_str(), b.content_type);
	hexdump(&ptfunxored[b.offset], b.block_size, level);

	for (vector<PTFFormat::block_t>::iterator c = b.child.begin();
			c != b.child.end(); ++c) {
		dump_block(*c, level + 1);
	}
}

void
PTFFormat::dump(void) {
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		switch (b->content_type) {
		case 0x1054:
			dump_block(*b, 0);
			break;
		default:
			break;
		}
	}
}

void
PTFFormat::parseblocks(void) {
	uint32_t i = 20;

	while (i < len) {
		struct block_t b;
		if (parse_block_at(i, &b, 0)) {
			blocks.push_back(b);
		}
		i += b.block_size ? b.block_size + 7 : 1;
	}
}

int
PTFFormat::parse(void) {
	parseblocks();
	//dump();
	if (!parseheader())
		return -1;
	setrates();
	if (sessionrate < 44100 || sessionrate > 192000)
		return -1;
	if (!parseaudio())
		return -1;
	if (!parserest())
		return -1;
	if (!parsemidi())
		return -1;
	return 0;
}

bool
PTFFormat::parseheader(void) {
	bool found = false;

	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x1028) {
			sessionrate = u_endian_read4(&ptfunxored[b->offset+4], is_bigendian);
			found = true;
		}
	}
	return found;
}

char *
PTFFormat::parsestring(uint32_t pos) {
	uint32_t length = u_endian_read4(&ptfunxored[pos], is_bigendian);
	pos += 4;
	return strndup((const char *)&ptfunxored[pos], length);
}

bool
PTFFormat::parseaudio(void) {
	bool found = false;
	uint32_t nwavs, i, n;
	uint32_t pos = 0;
	char *str;
	std::string wavtype;
	std::string wavname;

	// Parse wav names
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x1004) {

			nwavs = u_endian_read4(&ptfunxored[b->offset+2], is_bigendian);

			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x103a) {
					found = true;
					//nstrings = u_endian_read4(&ptfunxored[c->offset+1], is_bigendian);
					pos = c->offset + 11;
					// Found wav list
					for (i = n = 0; (pos < c->offset + c->block_size) && (n < nwavs); i++) {
						str = parsestring(pos);
						wavname = std::string(str);
						pos += wavname.size() + 4;
						str = strndup((const char *)&ptfunxored[pos], 4);
						wavtype = std::string(str);
						pos += 9;
						if (foundin(wavname, std::string(".grp")))
							continue;

						if (foundin(wavname, std::string("Audio Files"))) {
							continue;
						}
						if (foundin(wavname, std::string("Fade Files"))) {
							continue;
						}
						if (!(		foundin(wavtype, std::string("WAVE")) ||
								foundin(wavtype, std::string("EVAW")) ||
								foundin(wavtype, std::string("AIFF")) ||
								foundin(wavtype, std::string("FFIA"))) ) {
							continue;
						}
						wav_t f = { wavname, (uint16_t)n, 0, 0 };
						n++;
						actualwavs.push_back(f);
						audiofiles.push_back(f);
					}
				}
			}
		}
	}

	// Add wav length information
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x1004) {

			vector<PTFFormat::wav_t>::iterator wav = audiofiles.begin();

			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1003) {
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if (d->content_type == 0x1001) {
							(*wav).length = u_endian_read8(&ptfunxored[d->offset+8], is_bigendian);
							wav++;
						}
					}
				}
			}
		}
	}

	return found;
}


void
PTFFormat::parse_three_point(uint32_t j, uint64_t& start, uint64_t& offset, uint64_t& length) {
	uint8_t offsetbytes, lengthbytes, startbytes;

	if (is_bigendian) {
		offsetbytes = (ptfunxored[j+4] & 0xf0) >> 4;
		lengthbytes = (ptfunxored[j+3] & 0xf0) >> 4;
		startbytes = (ptfunxored[j+2] & 0xf0) >> 4;
		//somethingbytes = (ptfunxored[j+2] & 0xf);
		//skipbytes = ptfunxored[j+1];
	} else {
		offsetbytes = (ptfunxored[j+1] & 0xf0) >> 4; //3
		lengthbytes = (ptfunxored[j+2] & 0xf0) >> 4;
		startbytes = (ptfunxored[j+3] & 0xf0) >> 4; //1
		//somethingbytes = (ptfunxored[j+3] & 0xf);
		//skipbytes = ptfunxored[j+4];
	}

	switch (offsetbytes) {
	case 5:
		offset = u_endian_read5(&ptfunxored[j+5], false);
		break;
	case 4:
		offset = (uint64_t)u_endian_read4(&ptfunxored[j+5], false);
		break;
	case 3:
		offset = (uint64_t)u_endian_read3(&ptfunxored[j+5], false);
		break;
	case 2:
		offset = (uint64_t)u_endian_read2(&ptfunxored[j+5], false);
		break;
	case 1:
		offset = (uint64_t)(ptfunxored[j+5]);
		break;
	default:
		offset = 0;
		break;
	}
	j+=offsetbytes;
	switch (lengthbytes) {
	case 5:
		length = u_endian_read5(&ptfunxored[j+5], false);
		break;
	case 4:
		length = (uint64_t)u_endian_read4(&ptfunxored[j+5], false);
		break;
	case 3:
		length = (uint64_t)u_endian_read3(&ptfunxored[j+5], false);
		break;
	case 2:
		length = (uint64_t)u_endian_read2(&ptfunxored[j+5], false);
		break;
	case 1:
		length = (uint64_t)(ptfunxored[j+5]);
		break;
	default:
		length = 0;
		break;
	}
	j+=lengthbytes;
	switch (startbytes) {
	case 5:
		start = u_endian_read5(&ptfunxored[j+5], false);
		break;
	case 4:
		start = (uint64_t)u_endian_read4(&ptfunxored[j+5], false);
		break;
	case 3:
		start = (uint64_t)u_endian_read3(&ptfunxored[j+5], false);
		break;
	case 2:
		start = (uint64_t)u_endian_read2(&ptfunxored[j+5], false);
		break;
	case 1:
		start = (uint64_t)(ptfunxored[j+5]);
		break;
	default:
		start = 0;
		break;
	}
}

void
PTFFormat::parse_region_info(uint32_t j, block_t& blk, region_t& r) {
	uint64_t findex, start, sampleoffset, length;

	parse_three_point(j, start, sampleoffset, length);

	findex = u_endian_read4(&ptfunxored[blk.offset + blk.block_size], is_bigendian);
	wav_t f = {
		"",
		(uint16_t)findex,
		(int64_t)(start*ratefactor),
		(int64_t)(length*ratefactor),
	};

	vector<wav_t>::iterator found;
	if (find_wav(findex, found)) {
		f.filename = (*found).filename;
	}

	std::vector<midi_ev_t> m;
	r.startpos = (int64_t)(start*ratefactor);
	r.sampleoffset = (int64_t)(sampleoffset*ratefactor);
	r.length = (int64_t)(length*ratefactor);
	r.wave = f;
	r.midi = m;
}

bool
PTFFormat::parserest(void) {
	uint32_t i, j, count, n;
	uint64_t start;
	uint16_t rindex, rawindex, tindex;
	uint32_t nch;
	uint16_t ch_map[MAX_CHANNELS_PER_TRACK];
	bool found = false;
	char *reg;
	std::string regionname, trackname;
	rindex = 0;

	// Parse sources->regions
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x100b || b->content_type == 0x262a) {
			//nregions = u_endian_read4(&ptfunxored[b->offset+2], is_bigendian);
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1008 || c->content_type == 0x2629) {
					vector<PTFFormat::block_t>::iterator d = c->child.begin();
					region_t r;

					found = true;
					j = c->offset + 11;
					reg = parsestring(j);
					regionname = std::string(reg);
					j += regionname.size() + 4;

					r.name = regionname;
					r.index = rindex;
					parse_region_info(j, *d, r);

					regions.push_back(r);
					rindex++;
				}
			}
			found = true;
		}
	}

	// Parse tracks
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x1015) {
			//ntracks = u_endian_read4(&ptfunxored[b->offset+2], is_bigendian);
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1014) {
					j = c->offset + 2;
					reg = parsestring(j);
					trackname = std::string(reg);
					j += trackname.size() + 5;
					nch = u_endian_read4(&ptfunxored[j], is_bigendian);
					j += 4;
					for (i = 0; i < nch; i++) {
						ch_map[i] = u_endian_read2(&ptfunxored[j], is_bigendian);

						std::vector<track_t>::iterator ti;
						if (!find_track(ch_map[i], ti)) {
							// Add a dummy region for now
							wav_t w = { std::string(""), 0, 0, 0 };
							std::vector<midi_ev_t> m;
							region_t r = { std::string(""), 65535, 0, 0, 0, w, m};

							track_t t = {
								trackname,
								ch_map[i],
								uint8_t(0),
								r
							};
							tracks.push_back(t);
						}
						//printf("XXX %s : %d(%d)\n", reg, nch, ch_map[0]);
						j += 2;
					}
				}
			}
		} else if (b->content_type == 0x2519) {
			tindex = 0;
			//ntracks = u_endian_read4(&ptfunxored[b->offset+2], is_bigendian);
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x251a) {
					j = c->offset + 4;
					reg = parsestring(j);
					trackname = std::string(reg);
					j += trackname.size() + 4 + 18;
					//tindex = u_endian_read4(&ptfunxored[j], is_bigendian);

					std::vector<track_t>::iterator ti;

					// Add a dummy region for now
					wav_t w = { std::string(""), 0, 0, 0 };
					std::vector<midi_ev_t> m;
					region_t r = { std::string(""), 65535, 0, 0, 0, w, m};

					track_t t = {
						trackname,
						tindex,
						uint8_t(0),
						r
					};
					miditracks.push_back(t);
					tindex++;
				}
			}
		}
	}

	// Parse regions->tracks
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		tindex = 0;
		if (b->content_type == 0x1012) {
			//nregions = u_endian_read4(&ptfunxored[b->offset+2], is_bigendian);
			count = 0;
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1011) {
					reg = parsestring(c->offset + 2);
					regionname = std::string(reg);
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if (d->content_type == 0x100f) {
							for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
									e != d->child.end(); ++e) {
								if (e->content_type == 0x100e) {
									// Region->track
									std::vector<track_t>::iterator ti;
									std::vector<region_t>::iterator ri;
									j = e->offset + 4;
									rawindex = u_endian_read4(&ptfunxored[j], is_bigendian);
									if (!find_region(rawindex, ri))
										continue;
									if (!find_track(count, ti))
										continue;
									if ((*ti).reg.index == 65535) {
										(*ti).reg = *ri;
									} else {
										track_t t = *ti;
										t.reg = *ri;
										tracks.push_back(t);
									}
								}
							}
						}
					}
					found = true;
					count++;
				}
			}
		} else if (b->content_type == 0x1054) {
			//nregions = u_endian_read4(&ptfunxored[b->offset+2], is_bigendian);
			count = 0;
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1052) {
					reg = parsestring(c->offset + 2);
					regionname = std::string(reg);
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if (d->content_type == 0x1050) {
							for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
									e != d->child.end(); ++e) {
								if (e->content_type == 0x104f) {
									// Region->track
									std::vector<track_t>::iterator ti;
									std::vector<region_t>::iterator ri;
									j = e->offset + 4;
									rawindex = u_endian_read4(&ptfunxored[j], is_bigendian);
									j += 4 + 1;
									start = u_endian_read4(&ptfunxored[j], is_bigendian);
									tindex = count;
									if (!find_region(rawindex, ri)) {
										printf("XXX dropped region\n");
										continue;
									}
									if (!find_track(tindex, ti)) {
										printf("XXX dropped track %d\n", tindex);
										continue;
									}
									(*ri).startpos = start;
									if ((*ti).reg.index == 65535) {
										(*ti).reg = *ri;
									} else {
										track_t t = *ti;
										t.reg = *ri;
										tracks.push_back(t);
									}
								}
							}
						}
					}
					found = true;
					count++;
				}
			}
		} else if (b->content_type == 0x262c) {
			tindex = 0;
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x262b) {
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if (d->content_type == 0x2628) {
							count = 0;
							reg = parsestring(d->offset + 2);
							regionname = std::string(reg);
							j = d->offset + d->block_size + 1;
							n = u_endian_read4(&ptfunxored[j], is_bigendian);
							for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
									e != d->child.end(); ++e) {
								if (e->content_type == 0x2523) {
									// Real region
									j = e->offset + 39;
									rawindex = u_endian_read4(&ptfunxored[j], is_bigendian);
									printf("XXX %s : %d %d %d (%d)\n", reg, rawindex, tindex, n, rawindex-n);
									count++;
								}
							}
							if (!count) {
								// Compound region
								printf("XXX %s : compound region %d (%d)\n", reg, tindex, n);
							}
						}
						found = true;
					}
					tindex++;
				}
			}
		}
	}
	return found;
}

struct mchunk {
	mchunk (uint64_t zt, uint64_t ml, std::vector<PTFFormat::midi_ev_t> const& c)
	: zero (zt)
	, maxlen (ml)
	, chunk (c)
	{}
	uint64_t zero;
	uint64_t maxlen;
	std::vector<PTFFormat::midi_ev_t> chunk;
};

bool
PTFFormat::parsemidi(void) {
	uint32_t i, j, k, rindex, tindex, count, rawindex;
	uint64_t n_midi_events, zero_ticks, start;
	uint64_t midi_pos, midi_len, max_pos, region_pos;
	uint8_t midi_velocity, midi_note;
	uint16_t regionnumber = 0;
	std::string midiregionname;

	std::vector<mchunk> midichunks;
	midi_ev_t m;

	char *str;
	std::string regionname, trackname;
	rindex = 0;

	// Parse MIDI events
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x2000) {

			k = b->offset;

			// Parse all midi chunks, not 1:1 mapping to regions yet
			while (k + 35 < b->block_size + b->offset) {
				max_pos = 0;
				std::vector<midi_ev_t> midi;

				if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"MdNLB", 5)) {
					break;
				}
				k += 11;
				n_midi_events = u_endian_read4(&ptfunxored[k], is_bigendian);

				k += 4;
				zero_ticks = u_endian_read5(&ptfunxored[k], is_bigendian);
				for (i = 0; i < n_midi_events && k < len; i++, k += 35) {
					midi_pos = u_endian_read5(&ptfunxored[k], is_bigendian);
					midi_pos -= zero_ticks;
					midi_note = ptfunxored[k+8];
					midi_len = u_endian_read5(&ptfunxored[k+9], is_bigendian);
					midi_velocity = ptfunxored[k+17];

					if (midi_pos + midi_len > max_pos) {
						max_pos = midi_pos + midi_len;
					}

					m.pos = midi_pos;
					m.length = midi_len;
					m.note = midi_note;
					m.velocity = midi_velocity;
					midi.push_back(m);
				}
				midichunks.push_back(mchunk (zero_ticks, max_pos, midi));
			}

		// Put chunks onto regions
		} else if (b->content_type == 0x2002) {
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x2001) {
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if (d->content_type == 0x1007) {
							j = d->offset + 2;
							str = parsestring(d->offset + 2);
							midiregionname = std::string(str);
							j += 4 + midiregionname.size();
							parse_three_point(j, region_pos, zero_ticks, midi_len);
							j = d->offset + d->block_size;
							rindex = u_endian_read4(&ptfunxored[j], is_bigendian);
							struct mchunk mc = *(midichunks.begin()+rindex);

							wav_t w = { std::string(""), 0, 0, 0 };
							region_t r = {
								midiregionname,
								regionnumber++,
								(int64_t)0xe8d4a51000ULL,
								(int64_t)0,
								//(int64_t)(max_pos*sessionrate*60/(960000*120)),
								(int64_t)mc.maxlen,
								w,
								mc.chunk,
							};
							midiregions.push_back(r);
							//printf("XXX MIDI %s : r(%d) (%llu, %llu, %llu)\n", str, rindex, zero_ticks, region_pos, midi_len);
							//dump_block(*d, 1);
						}
					}
				}
			}
		// Put midi regions onto midi tracks 
		} else if (b->content_type == 0x1058) {
			//nregions = u_endian_read4(&ptfunxored[b->offset+2], is_bigendian);
			count = 0;
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1057) {
					str = parsestring(c->offset + 2);
					regionname = std::string(str);
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if (d->content_type == 0x1056) {
							for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
									e != d->child.end(); ++e) {
								if (e->content_type == 0x104f) {
									// MIDI region->MIDI track
									std::vector<track_t>::iterator ti;
									std::vector<region_t>::iterator ri;
									j = e->offset + 4;
									rawindex = u_endian_read4(&ptfunxored[j], is_bigendian);
									j += 4 + 1;
									start = u_endian_read5(&ptfunxored[j], is_bigendian);
									tindex = count;
									if (!find_midiregion(rawindex, ri)) {
										printf("XXX dropped region\n");
										continue;
									}
									if (!find_miditrack(tindex, ti)) {
										printf("XXX dropped t(%d) r(%d)\n", tindex, rawindex);
										continue;
									}
									//printf("XXX MIDI : %s : t(%d) r(%d) %llu(%llu)\n", (*ti).name.c_str(), tindex, rawindex, start, (*ri).startpos);
									int64_t signedstart = (int64_t)(start - ZERO_TICKS);
									if (signedstart < 0)
										signedstart = -signedstart;
									(*ri).startpos = (uint64_t)signedstart;
									if ((*ti).reg.index == 65535) {
										(*ti).reg = *ri;
									} else {
										track_t t = *ti;
										t.reg = *ri;
										miditracks.push_back(t);
									}
								}
							}
						}
					}
					count++;
				}
			}
		}
	}
	for (std::vector<track_t>::iterator tr = miditracks.begin();
			tr != miditracks.end(); ++tr) {
		if ((*tr).reg.index == 65535) {
			miditracks.erase(tr--);
		}
	}
	return true;
}

#if 0
int
PTFFormat::parse(void) { 
	if (version == 5) {
		parse5header();
		setrates();
		if (sessionrate < 44100 || sessionrate > 192000)
		  return -1;
		parseaudio5();
		parserest5();
		parsemidi();
	} else if (version == 7) {
		parse7header();
		setrates();
		if (sessionrate < 44100 || sessionrate > 192000)
		  return -1;
		parseaudio();
		parserest89();
		parsemidi();
	} else if (version == 8) {
		parse8header();
		setrates();
		if (sessionrate < 44100 || sessionrate > 192000)
		  return -1;
		parseaudio();
		parserest89();
		parsemidi();
	} else if (version == 9) {
		parse9header();
		setrates();
		if (sessionrate < 44100 || sessionrate > 192000)
		  return -1;
		parseaudio();
		parserest89();
		parsemidi();
	} else if (version == 10 || version == 11 || version == 12) {
		parse10header();
		setrates();
		if (sessionrate < 44100 || sessionrate > 192000)
		  return -1;
		parseaudio();
		parserest12();
		parsemidi12();
	} else {
		// Should not occur
		return -1;
	}
	return 0;
}
void
PTFFormat::parse5header(void) {
	uint32_t k;

	// Find session sample rate
	k = 0x100;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x00\x02", 3)) {
		jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x03", 2);
		k--;
	}

	sessionrate = u_endian_read3(&ptfunxored[k+12], is_bigendian);
}

void
PTFFormat::parse7header(void) {
	uint32_t k;

	// Find session sample rate
	k = 0x100;

	jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x00\x05", 3);

	sessionrate = u_endian_read3(&ptfunxored[k+12], is_bigendian);
}

void
PTFFormat::parse8header(void) {
	uint32_t k;

	// Find session sample rate
	k = 0;

	jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x05", 2);

	sessionrate = u_endian_read3(&ptfunxored[k+11], is_bigendian);
}

void
PTFFormat::parse9header(void) {
	uint32_t k;

	// Find session sample rate
	k = 0x100;

	jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x06", 2);

	sessionrate = u_endian_read3(&ptfunxored[k+11], is_bigendian);
}

void
PTFFormat::parse10header(void) {
	uint32_t k;

	// Find session sample rate
	k = 0x100;

	jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x09", 2);

	sessionrate = u_endian_read3(&ptfunxored[k+11], is_bigendian);
}

void
PTFFormat::parserest5(void) {
	uint32_t i, j, k;
	uint64_t regionspertrack, lengthofname, numberofregions;
	uint64_t startbytes, lengthbytes, offsetbytes, somethingbytes, skipbytes;
	uint16_t tracknumber = 0;
	uint16_t findex;
	uint16_t rindex = 0;
	unsigned char tag1[3];
	unsigned char tag2[3];
	unsigned char tag3[3];

	if (is_bigendian) {
		tag1[0] = tag2[0] = tag3[0] = '\x5a';
		tag1[1] = tag2[1] = tag3[1] = '\x00';
		tag1[2] = '\x01';
		tag2[2] = '\x02';
		tag3[2] = '\x03';
	} else {
		tag1[0] = tag2[0] = tag3[0] = '\x5a';
		tag1[1] = '\x01';
		tag2[1] = '\x02';
		tag3[1] = '\x03';
		tag1[2] = tag2[2] = tag3[2] = '\x00';
	}

	// Find Source->Region info
	k = upto;
	for (i = 0; i < 2; i++) {
		jumpto(&k, ptfunxored, len, tag3, 3);
		k++;
	}
	jumpto(&k, ptfunxored, len, tag2, 3);

	numberofregions = u_endian_read4(&ptfunxored[k-13], is_bigendian);

	i = k;
	while (numberofregions > 0 && i < len) {
		jumpto(&i, ptfunxored, len, tag2, 3);

		uint32_t lengthofname = u_endian_read4(&ptfunxored[i+9], is_bigendian);

		char name[256] = {0};
		for (j = 0; j < lengthofname; j++) {
			name[j] = ptfunxored[i+13+j];
		}
		name[j] = '\0';
		j += i+13;
		//uint8_t disabled = ptfunxored[j];

		if (is_bigendian) {
			offsetbytes = (ptfunxored[j+4] & 0xf0) >> 4;
			lengthbytes = (ptfunxored[j+3] & 0xf0) >> 4;
			startbytes = (ptfunxored[j+2] & 0xf0) >> 4;
			somethingbytes = (ptfunxored[j+2] & 0xf);
			skipbytes = ptfunxored[j+1];
		} else {
			offsetbytes = (ptfunxored[j+1] & 0xf0) >> 4; //3
			lengthbytes = (ptfunxored[j+2] & 0xf0) >> 4;
			startbytes = (ptfunxored[j+3] & 0xf0) >> 4; //1
			somethingbytes = (ptfunxored[j+3] & 0xf);
			skipbytes = ptfunxored[j+4];
		}
		findex = u_endian_read4(&ptfunxored[j+5
				+startbytes
				+lengthbytes
				+offsetbytes
				+somethingbytes
				+skipbytes], is_bigendian);

		uint32_t sampleoffset = 0;
		switch (offsetbytes) {
		case 4:
			sampleoffset = u_endian_read4(&ptfunxored[j+5], false);
			break;
		case 3:
			sampleoffset = u_endian_read3(&ptfunxored[j+5], false);
			break;
		case 2:
			sampleoffset = (uint32_t)u_endian_read2(&ptfunxored[j+5], false);
			break;
		case 1:
			sampleoffset = (uint32_t)(ptfunxored[j+5]);
			break;
		default:
			break;
		}
		j+=offsetbytes;
		uint32_t length = 0;
		switch (lengthbytes) {
		case 4:
			length = u_endian_read4(&ptfunxored[j+5], false);
			break;
		case 3:
			length = u_endian_read3(&ptfunxored[j+5], false);
			break;
		case 2:
			length = (uint32_t)u_endian_read2(&ptfunxored[j+5], false);
			break;
		case 1:
			length = (uint32_t)(ptfunxored[j+5]);
			break;
		default:
			break;
		}
		j+=lengthbytes;
		uint32_t start = 0;
		switch (startbytes) {
		case 4:
			start = u_endian_read4(&ptfunxored[j+5], false);
			break;
		case 3:
			start = u_endian_read3(&ptfunxored[j+5], false);
			break;
		case 2:
			start = (uint32_t)u_endian_read2(&ptfunxored[j+5], false);
			break;
		case 1:
			start = (uint32_t)(ptfunxored[j+5]);
			break;
		default:
			break;
		}
		j+=startbytes;

		std::string filename = string(name);
		wav_t f = {
			"",
			(uint16_t)findex,
			(int64_t)(start*ratefactor),
			(int64_t)(length*ratefactor),
		};

		vector<wav_t>::iterator begin = actualwavs.begin();
		vector<wav_t>::iterator finish = actualwavs.end();
		vector<wav_t>::iterator found;
		if ((found = std::find(begin, finish, f)) != finish) {
			f.filename = (*found).filename;
		}
		std::vector<midi_ev_t> m;
		region_t r = {
			name,
			rindex,
			(int64_t)(start*ratefactor),
			(int64_t)(sampleoffset*ratefactor),
			(int64_t)(length*ratefactor),
			f,
			m
		};
		regions.push_back(r);
		rindex++;
		i = j + 1;
		numberofregions--;
	}

	// Find Region->Track info
	/*
	k = 0;
	for (i = 0; i < 4; i++) {
		jumpto(&k, ptfunxored, len, tag3, 3);
		k++;
	}
	*/
	k = j = i;
	while (j < len) {
		if (jumpto(&j, ptfunxored, j+13+3, tag1, 3)) {
			j++;
			if (jumpto(&j, ptfunxored, j+13+3, tag1, 3)) {
				j++;
				if (jumpto(&j, ptfunxored, j+13+3, tag1, 3)) {
					if ((j == k+26) && (ptfunxored[j-13] == '\x5a') && (ptfunxored[j-26] == '\x5a')) {
						k = j;
						break;
					}
				}
			}
		}
		k++;
		j = k;
	}

	if (ptfunxored[k+13] == '\x5a') {
		k++;
	}

	// Start parsing track info
	rindex = 0;
	while (k < len) {
		if (		(ptfunxored[k  ] == 0xff) &&
				(ptfunxored[k+1] == 0xff)) {
			break;
		}
		jumpto(&k, ptfunxored, len, tag1, 3);

		lengthofname = u_endian_read4(&ptfunxored[k+9], is_bigendian);
		if (ptfunxored[k+13] == 0x5a) {
			k++;
			break;
		}
		char name[256] = {0};
		for (j = 0; j < lengthofname; j++) {
			name[j] = ptfunxored[k+13+j];
		}
		name[j] = '\0';
		regionspertrack = u_endian_read4(&ptfunxored[k+13+j], is_bigendian);
		for (i = 0; i < regionspertrack; i++) {
			jumpto(&k, ptfunxored, len, tag3, 3);
			j = k + 15;
			if (is_bigendian) {
				offsetbytes = (ptfunxored[j+1] & 0xf0) >> 4;
				//somethingbytes = (ptfunxored[j+2] & 0xf);
				lengthbytes = (ptfunxored[j+3] & 0xf0) >> 4;
				startbytes = (ptfunxored[j+4] & 0xf0) >> 4;
			} else {
				offsetbytes = (ptfunxored[j+4] & 0xf0) >> 4;
				//somethingbytes = (ptfunxored[j+3] & 0xf);
				lengthbytes = (ptfunxored[j+2] & 0xf0) >> 4;
				startbytes = (ptfunxored[j+1] & 0xf0) >> 4;
			}
			rindex = u_endian_read4(&ptfunxored[k+11], is_bigendian);
			uint32_t start = 0;
			switch (startbytes) {
			case 4:
				start = u_endian_read4(&ptfunxored[j+5], false); 
				break;
			case 3:
				start = u_endian_read3(&ptfunxored[j+5], false); 
				break;
			case 2:
				start = (uint32_t)u_endian_read2(&ptfunxored[j+5], false); 
				break;
			case 1:
				start = (uint32_t)(ptfunxored[j+5]);
				break;
			default:
				break;
			}
			j+=startbytes;
			uint32_t length = 0;
			switch (lengthbytes) {
			case 4:
				length = u_endian_read4(&ptfunxored[j+5], false);
				break;
			case 3:
				length = u_endian_read3(&ptfunxored[j+5], false);
				break;
			case 2:
				length = (uint32_t)u_endian_read2(&ptfunxored[j+5], false);
				break;
			case 1:
				length = (uint32_t)(ptfunxored[j+5]);
				break;
			default:
				break;
			}
			j+=lengthbytes;
			uint32_t sampleoffset = 0;
			switch (offsetbytes) {
			case 4:
				sampleoffset = u_endian_read4(&ptfunxored[j+5], false); 
				break;
			case 3:
				sampleoffset = u_endian_read3(&ptfunxored[j+5], false); 
				break;
			case 2:
				sampleoffset = (uint32_t)u_endian_read2(&ptfunxored[j+5], false); 
				break;
			case 1:
				sampleoffset = (uint32_t)(ptfunxored[j+5]);
				break;
			default:
				break;
			}
			j+=offsetbytes;

			track_t tr;
			tr.name = name;

			region_t r;
			r.index = rindex;

			vector<region_t>::iterator begin = regions.begin();
			vector<region_t>::iterator finish = regions.end();
			vector<region_t>::iterator found;
			if ((found = std::find(begin, finish, r)) != finish) {
				tr.reg = (*found);
			}

			tr.reg.startpos = (int64_t)(start*ratefactor);
			tr.reg.sampleoffset = (int64_t)(sampleoffset*ratefactor);
			tr.reg.length = (int64_t)(length*ratefactor);
			vector<track_t>::iterator ti;
			vector<track_t>::iterator bt = tracks.begin();
			vector<track_t>::iterator et = tracks.end();
			if ((ti = std::find(bt, et, tr)) != et) {
				tracknumber = (*ti).index;
			} else {
				++tracknumber;
			}
			track_t t = {
				name,
				(uint16_t)tracknumber,
				uint8_t(0),
				tr.reg
			};
			//if (tr.reg.length > 0) {
				tracks.push_back(t);
			//}
			k++;
		}
		k++;
	}
}

void
PTFFormat::resort(std::vector<wav_t>& ws) {
	int j = 0;
	std::sort(ws.begin(), ws.end());
	for (std::vector<wav_t>::iterator i = ws.begin(); i != ws.end(); ++i) {
		(*i).index = j;
		j++;
	}
}

void
PTFFormat::resort(std::vector<region_t>& rs) {
	int j = 0;
	//std::sort(rs.begin(), rs.end());
	for (std::vector<region_t>::iterator i = rs.begin(); i != rs.end(); ++i) {
		(*i).index = j;
		j++;
	}
}

void
PTFFormat::filter(std::vector<region_t>& rs) {
	for (std::vector<region_t>::iterator i = rs.begin(); i != rs.end(); ++i) {
		if (i->length == 0)
			rs.erase(i);
	}
}

void
PTFFormat::parseaudio5(void) {
	uint32_t i,k,l;
	uint64_t lengthofname, wavnumber;
	uint32_t numberofwavs;
	unsigned char tag6_LE[3], tag5_BE[3];
	unsigned char tag2_LE[3], tag2_BE[3];

	// Find start of wav file list
	k = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\xff\xff\xff\xff", 4))
		return;
	numberofwavs = u_endian_read4(&ptfunxored[k-18], is_bigendian);

	// Find actual wav names
	char wavname[256];
	i = k;
	jumpto(&i, ptfunxored, len, (const unsigned char *)"Files", 5);

	wavnumber = 0;
	i+=16;
	char ext[5];
	while (i < len && numberofwavs > 0) {
		i++;
		if (		((ptfunxored[i  ] == 0x5a) &&
				(ptfunxored[i+1] == 0x00) &&
				(ptfunxored[i+2] == 0x05)) ||
				((ptfunxored[i  ] == 0x5a) &&
				(ptfunxored[i+1] == 0x06))) {
			break;
		}
		lengthofname = u_endian_read4(&ptfunxored[i-3], is_bigendian);
		i++;
		l = 0;
		while (l < lengthofname) {
			wavname[l] = ptfunxored[i+l];
			l++;
		}
		i+=lengthofname;
		ext[0] = ptfunxored[i++];
		ext[1] = ptfunxored[i++];
		ext[2] = ptfunxored[i++];
		ext[3] = ptfunxored[i++];
		ext[4] = '\0';

		wavname[l] = 0;
		if (foundin(wavname, ".L") || foundin(wavname, ".R")) {
			extension = string("");
		} else if (foundin(wavname, ".wav") || foundin(ext, "WAVE")) {
			extension = string(".wav");
		} else if (foundin(wavname, ".aif") || foundin(ext, "AIFF")) {
			extension = string(".aif");
		} else {
			extension = string("");
		}

		std::string wave = string(wavname);

		if (foundin(wave, string(".grp"))) {
			continue;
		}
		if (foundin(wave, string("Fade Files"))) {
			i += 7;
			continue;
		}

		wav_t f = { wave, (uint16_t)(wavnumber++), 0, 0 };

		actualwavs.push_back(f);
		audiofiles.push_back(f);
		//printf("done\n");
		numberofwavs--;
		i += 7;
	}

	i -= 7;

	tag5_BE[0] = tag6_LE[0] = tag2_BE[0] = tag2_LE[0] = '\x5a';
	tag5_BE[1] = '\x00';
	tag6_LE[1] = '\x06';
	tag2_BE[1] = '\x00';
	tag2_LE[1] = '\x02';
	tag5_BE[2] = '\x05';
	tag6_LE[2] = '\x00';
	tag2_BE[2] = '\x02';
	tag2_LE[2] = '\x00';

	// Loop through all the sources
	for (vector<wav_t>::iterator w = audiofiles.begin(); w != audiofiles.end(); ++w) {
		// Jump to start of source metadata for this source
		if (is_bigendian) {
			if (!jumpto(&i, ptfunxored, len, tag5_BE, 3))
				return;
			if (!jumpto(&i, ptfunxored, len, tag2_BE, 3))
				return;
			w->length = u_endian_read4(&ptfunxored[i+19], true);
		} else {
			if (!jumpto(&i, ptfunxored, len, tag6_LE, 3))
				return;
			if (!jumpto(&i, ptfunxored, len, tag2_LE, 3))
				return;
			w->length = u_endian_read4(&ptfunxored[i+15], false);
		}
	}
	upto = i;
}

void
PTFFormat::parsemidi(void) {
	uint32_t i, k;
	uint64_t tr, n_midi_events, zero_ticks;
	uint64_t midi_pos, midi_len, max_pos, region_pos;
	uint8_t midi_velocity, midi_note;
	uint16_t ridx;
	uint16_t nmiditracks, regionnumber = 0;
	uint32_t nregions, mr;

	std::vector<mchunk> midichunks;
	midi_ev_t m;

	// Find MdNLB
	k = 0;

	// Parse all midi chunks, not 1:1 mapping to regions yet
	while (k + 35 < len) {
		max_pos = 0;
		std::vector<midi_ev_t> midi;

		if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"MdNLB", 5)) {
			break;
		}
		k += 11;
		n_midi_events = ptfunxored[k] | ptfunxored[k+1] << 8 |
				ptfunxored[k+2] << 16 | ptfunxored[k+3] << 24;

		k += 4;
		zero_ticks = u_endian_read5(&ptfunxored[k], is_bigendian);
		for (i = 0; i < n_midi_events && k < len; i++, k += 35) {
			midi_pos = u_endian_read5(&ptfunxored[k], is_bigendian);
			midi_pos -= zero_ticks;
			midi_note = ptfunxored[k+8];
			midi_len = u_endian_read5(&ptfunxored[k+9], is_bigendian);
			midi_velocity = ptfunxored[k+17];

			if (midi_pos + midi_len > max_pos) {
				max_pos = midi_pos + midi_len;
			}

			m.pos = midi_pos;
			m.length = midi_len;
			m.note = midi_note;
			m.velocity = midi_velocity;
#if 1
// stop gap measure to prevent crashes in ardour,
// remove when decryption is fully solved for .ptx
			if ((m.velocity & 0x80) || (m.note & 0x80) ||
					(m.pos & 0xff00000000LL) || (m.length & 0xff00000000LL)) {
				continue;
			}
#endif
			midi.push_back(m);
		}
		midichunks.push_back(mchunk (zero_ticks, max_pos, midi));
	}

	// Map midi chunks to regions
	while (k < len) {
		char midiregionname[256];
		uint8_t namelen;

		if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"MdTEL", 5)) {
			break;
		}

		k += 41;

		nregions = u_endian_read2(&ptfunxored[k], is_bigendian);

		for (mr = 0; mr < nregions; mr++) {
			if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x0c", 2)) {
				break;
			}

			k += 9;

			namelen = ptfunxored[k];
			for (i = 0; i < namelen; i++) {
				midiregionname[i] = ptfunxored[k+4+i];
			}
			midiregionname[namelen] = '\0';
			k += 4 + namelen;

			k += 5;
			/*
			region_pos = (uint64_t)ptfunxored[k] |
					(uint64_t)ptfunxored[k+1] << 8 |
					(uint64_t)ptfunxored[k+2] << 16 |
					(uint64_t)ptfunxored[k+3] << 24 |
					(uint64_t)ptfunxored[k+4] << 32;
			*/
			if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\xfe\xff\xff\xff", 4)) {
				break;
			}

			k += 40;

			ridx = ptfunxored[k];
			ridx |= ptfunxored[k+1] << 8;

			struct mchunk mc = *(midichunks.begin()+ridx);

			wav_t w = { std::string(""), 0, 0, 0 };
			region_t r = {
				midiregionname,
				regionnumber++,
				//(int64_t)mc.zero,
				(int64_t)0xe8d4a51000ULL,
				(int64_t)(0),
				//(int64_t)(max_pos*sessionrate*60/(960000*120)),
				(int64_t)mc.maxlen,
				w,
				mc.chunk,
			};
			midiregions.push_back(r);
		}
	}

	// Put midi regions on midi tracks
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x03", 2)) {
		return;
	}

	nmiditracks = u_endian_read2(&ptfunxored[k-4], is_bigendian);

	for (tr = 0; tr < nmiditracks; tr++) {
		char miditrackname[256];
		uint8_t namelen;
		if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x03", 2)) {
			return;
		}

		namelen = ptfunxored[k+9];
		for (i = 0; i < namelen; i++) {
			miditrackname[i] = ptfunxored[k+13+i];
		}
		miditrackname[namelen] = '\0';
		k += 13 + namelen;
		nregions = u_endian_read2(&ptfunxored[k], is_bigendian);

		for (i = 0; (i < nregions) && (k < len); i++) {
			k += 24;

			ridx = u_endian_read2(&ptfunxored[k], is_bigendian);

			k += 5;

			region_pos = u_endian_read5(&ptfunxored[k], is_bigendian);

			k += 20;

			track_t mtr;
			mtr.name = string(miditrackname);
			mtr.index = tr;
			mtr.playlist = 0;
			// Find the midi region with index 'ridx'
			std::vector<region_t>::iterator begin = midiregions.begin();
			std::vector<region_t>::iterator finish = midiregions.end();
			std::vector<region_t>::iterator mregion;
			wav_t w = { std::string(""), 0, 0, 0 };
			std::vector<midi_ev_t> m;
			region_t r = { std::string(""), ridx, 0, 0, 0, w, m};
			if ((mregion = std::find(begin, finish, r)) != finish) {
				mtr.reg = *mregion;
				mtr.reg.startpos = region_pos - mtr.reg.startpos;
				miditracks.push_back(mtr);
			}
		}
	}
}

void
PTFFormat::parsemidi12(void) {
	uint32_t i, k;
	uint64_t tr, n_midi_events, zero_ticks;
	uint64_t midi_pos, midi_len, max_pos, region_pos;
	uint8_t midi_velocity, midi_note;
	uint16_t ridx;
	uint16_t nmiditracks, regionnumber = 0;
	uint32_t nregions, mr;

	std::vector<mchunk> midichunks;
	midi_ev_t m;

	k = 0;

	// Parse all midi chunks, not 1:1 mapping to regions yet
	while (k + 35 < len) {
		max_pos = 0;
		std::vector<midi_ev_t> midi;

		// Find MdNLB
		if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"MdNLB", 5)) {
			break;
		}

		k += 11;
		n_midi_events = u_endian_read4(&ptfunxored[k], is_bigendian);

		k += 4;
		zero_ticks = u_endian_read5(&ptfunxored[k], is_bigendian);
		for (i = 0; i < n_midi_events && k < len; i++, k += 35) {
			midi_pos = u_endian_read5(&ptfunxored[k], is_bigendian);
			midi_pos -= zero_ticks;
			midi_note = ptfunxored[k+8];
			midi_len = u_endian_read5(&ptfunxored[k+9], is_bigendian);
			midi_velocity = ptfunxored[k+17];

			if (midi_pos + midi_len > max_pos) {
				max_pos = midi_pos + midi_len;
			}

			m.pos = midi_pos;
			m.length = midi_len;
			m.note = midi_note;
			m.velocity = midi_velocity;
#if 1
// stop gap measure to prevent crashes in ardour,
// remove when decryption is fully solved for .ptx
			if ((m.velocity & 0x80) || (m.note & 0x80) ||
					(m.pos & 0xff00000000LL) || (m.length & 0xff00000000LL)) {
				continue;
			}
#endif
			midi.push_back(m);
		}
		midichunks.push_back(mchunk (zero_ticks, max_pos, midi));
	}

	// Map midi chunks to regions
	while (k < len) {
		char midiregionname[256];
		uint8_t namelen;

		if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"MdTEL", 5)) {
			break;
		}

		k += 41;

		nregions = u_endian_read2(&ptfunxored[k], is_bigendian);

		for (mr = 0; mr < nregions; mr++) {
			if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x01", 2)) {
				break;
			}
			k += 18;

			namelen = ptfunxored[k];
			for (i = 0; i < namelen; i++) {
				midiregionname[i] = ptfunxored[k+4+i];
			}
			midiregionname[namelen] = '\0';
			k += 4 + namelen;

			k += 5;
			/*
			region_pos = (uint64_t)ptfunxored[k] |
					(uint64_t)ptfunxored[k+1] << 8 |
					(uint64_t)ptfunxored[k+2] << 16 |
					(uint64_t)ptfunxored[k+3] << 24 |
					(uint64_t)ptfunxored[k+4] << 32;
			*/
			if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\xfe\xff\x00\x00", 4)) {
				break;
			}

			k += 37;

			ridx = u_endian_read2(&ptfunxored[k], is_bigendian);

			k += 3;
			struct mchunk mc = *(midichunks.begin()+ridx);

			wav_t w = { std::string(""), 0, 0, 0 };
			region_t r = {
				midiregionname,
				regionnumber++,
				//(int64_t)mc.zero,
				(int64_t)0xe8d4a51000ULL,
				(int64_t)(0),
				//(int64_t)(max_pos*sessionrate*60/(960000*120)),
				(int64_t)mc.maxlen,
				w,
				mc.chunk,
			};
			midiregions.push_back(r);
		}
	}

	// Put midi regions on midi tracks
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x03", 2)) {
		return;
	}

	nmiditracks = u_endian_read2(&ptfunxored[k-4], is_bigendian);

	for (tr = 0; tr < nmiditracks; tr++) {
		char miditrackname[256];
		uint8_t namelen;
		if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x03", 2)) {
			return;
		}

		namelen = ptfunxored[k+9];
		for (i = 0; i < namelen; i++) {
			miditrackname[i] = ptfunxored[k+13+i];
		}
		miditrackname[namelen] = '\0';
		k += 13 + namelen;
		nregions = u_endian_read2(&ptfunxored[k], is_bigendian);

		k += 13;

		for (i = 0; (i < nregions) && (k < len); i++) {
			while (k < len) {
				if (		(ptfunxored[k] == 0x5a) &&
						(ptfunxored[k+1] & 0x08)) {
					break;
				}
				k++;
			}
			k += 11;

			ridx = u_endian_read2(&ptfunxored[k], is_bigendian);

			k += 5;

			region_pos = u_endian_read5(&ptfunxored[k], is_bigendian);

			track_t mtr;
			mtr.name = string(miditrackname);
			mtr.index = tr;
			mtr.playlist = 0;
			// Find the midi region with index 'ridx'
			std::vector<region_t>::iterator begin = midiregions.begin();
			std::vector<region_t>::iterator finish = midiregions.end();
			std::vector<region_t>::iterator mregion;
			wav_t w = { std::string(""), 0, 0, 0 };
			std::vector<midi_ev_t> m;
			region_t r = { std::string(""), ridx, 0, 0, 0, w, m};
			if ((mregion = std::find(begin, finish, r)) != finish) {
				mtr.reg = *mregion;
				mtr.reg.startpos = region_pos - mtr.reg.startpos;
				miditracks.push_back(mtr);
			}
			if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\xff\xff\xff\xff\xff\xff\xff\xff", 8)) {
				return;
			}
		}
	}
}

void
PTFFormat::parseaudio(void) {
	uint32_t i,j,k,l;
	std::string wave;

	k = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"Audio Files", 11))
		return;

	// Find end of wav file list
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\xff\xff\xff\xff", 4))
		return;

	// Find number of wave files
	uint16_t numberofwavs;
	j = k;
	if (!jumpback(&j, ptfunxored, len, (const unsigned char *)"\x5a\x01", 2))
		return;

	numberofwavs = u_endian_read4(&ptfunxored[j-4], is_bigendian);
	//printf("%d wavs\n", numberofwavs);

	// Find actual wav names
	char wavname[256];
	j = k - 2;
	for (i = 0; i < numberofwavs; i++) {
		while (j > 0) {
			if (	((ptfunxored[j  ] == 'W') || (ptfunxored[j  ] == 'A') || ptfunxored[j  ] == '\0') &&
				((ptfunxored[j-1] == 'A') || (ptfunxored[j-1] == 'I') || ptfunxored[j-1] == '\0') &&
				((ptfunxored[j-2] == 'V') || (ptfunxored[j-2] == 'F') || ptfunxored[j-2] == '\0')) {
				break;
			}
			j--;
		}
		j -= 4;
		l = 0;
		while (ptfunxored[j] != '\0') {
			wavname[l] = ptfunxored[j];
			l++;
			j--;
		}
		wavname[l] = '\0';

		// Must be at least "vaw.z\0"
		if (l < 6) {
			i--;
			continue;
		}

		// and skip "zWAVE" or "zAIFF"
		if (	(	(wavname[1] == 'W') &&
				(wavname[2] == 'A') &&
				(wavname[3] == 'V') &&
				(wavname[4] == 'E')) ||
			(	(wavname[1] == 'A') &&
				(wavname[2] == 'I') &&
				(wavname[3] == 'F') &&
				(wavname[4] == 'F'))) {
			wave = string(&wavname[5]);
		} else {
			wave = string(wavname);
		}
		//uint8_t playlist = ptfunxored[j-8];

		std::reverse(wave.begin(), wave.end());
		wav_t f = { wave, (uint16_t)(numberofwavs - i - 1), 0, 0 };

		if (foundin(wave, string("Audio Files")) ||
				foundin(wave, string("Fade Files"))) {
			i--;
			continue;
		}

		actualwavs.push_back(f);
		audiofiles.push_back(f);

		//printf(" %d:%s \n", numberofwavs - i - 1, wave.c_str());
	}
	std::reverse(audiofiles.begin(), audiofiles.end());
	std::reverse(actualwavs.begin(), actualwavs.end());
	//resort(audiofiles);
	//resort(actualwavs);

	// Jump to end of wav file list
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\xff\xff\xff\xff", 4))
		return;

	// Loop through all the sources
	for (vector<wav_t>::iterator w = audiofiles.begin(); w != audiofiles.end(); ++w) {
		// Jump to start of source metadata for this source
		if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x07", 2))
			return;
		if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2))
			return;
		k++;
		if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2))
			return;

		w->length = u_endian_read4(&ptfunxored[k-25], false);
	}
}

void
PTFFormat::parserest89(void) {
	uint32_t i,j,k;
	uint8_t startbytes = 0;
	uint8_t lengthbytes = 0;
	uint8_t offsetbytes = 0;
	uint8_t somethingbytes = 0;
	uint8_t skipbytes = 0;

	// Find Regions
	k = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"Snap", 4)) {
		return;
	}

	uint16_t rindex = 0;
	uint32_t findex = 0;
	for (i = k; i < len-70; i++) {
		if (		(ptfunxored[i  ] == 0x5a) &&
				(ptfunxored[i+1] == 0x0a)) {
				break;
		}
		if (		(ptfunxored[i  ] == 0x5a) &&
				(ptfunxored[i+1] == 0x0c)) {

			uint8_t lengthofname = ptfunxored[i+9];

			char name[256] = {0};
			for (j = 0; j < lengthofname; j++) {
				name[j] = ptfunxored[i+13+j];
			}
			name[j] = '\0';
			j += i+13;
			//uint8_t disabled = ptfunxored[j];

			offsetbytes = (ptfunxored[j+1] & 0xf0) >> 4;
			lengthbytes = (ptfunxored[j+2] & 0xf0) >> 4;
			startbytes = (ptfunxored[j+3] & 0xf0) >> 4;
			somethingbytes = (ptfunxored[j+3] & 0xf);
			skipbytes = ptfunxored[j+4];
			findex = ptfunxored[j+5
					+startbytes
					+lengthbytes
					+offsetbytes
					+somethingbytes
					+skipbytes
					+40];
			/*rindex = ptfunxored[j+5
					+startbytes
					+lengthbytes
					+offsetbytes
					+somethingbytes
					+skipbytes
					+24];
			*/
			uint32_t sampleoffset = 0;
			switch (offsetbytes) {
			case 4:
				sampleoffset = u_endian_read4(&ptfunxored[j+5], false);
				break;
			case 3:
				sampleoffset = u_endian_read3(&ptfunxored[j+5], false);
				break;
			case 2:
				sampleoffset = (uint32_t)u_endian_read2(&ptfunxored[j+5], false);
				break;
			case 1:
				sampleoffset = (uint32_t)(ptfunxored[j+5]);
				break;
			default:
				break;
			}
			j+=offsetbytes;
			uint32_t length = 0;
			switch (lengthbytes) {
			case 4:
				length = u_endian_read4(&ptfunxored[j+5], false);
				break;
			case 3:
				length = u_endian_read3(&ptfunxored[j+5], false);
				break;
			case 2:
				length = (uint32_t)u_endian_read2(&ptfunxored[j+5], false);
				break;
			case 1:
				length = (uint32_t)(ptfunxored[j+5]);
				break;
			default:
				break;
			}
			j+=lengthbytes;
			uint32_t start = 0;
			switch (startbytes) {
			case 4:
				start = u_endian_read4(&ptfunxored[j+5], false);
				break;
			case 3:
				start = u_endian_read3(&ptfunxored[j+5], false);
				break;
			case 2:
				start = (uint32_t)u_endian_read2(&ptfunxored[j+5], false);
				break;
			case 1:
				start = (uint32_t)(ptfunxored[j+5]);
				break;
			default:
				break;
			}
			j+=startbytes;
			/*
			uint32_t something = 0;
			switch (somethingbytes) {
			case 4:
				something |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				something |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				something |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				something |= (uint32_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=somethingbytes;
			*/
			std::string filename = string(name);
			wav_t f = {
				filename,
				(uint16_t)findex,
				(int64_t)(start*ratefactor),
				(int64_t)(length*ratefactor),
			};

			//printf("something=%d\n", something);

			vector<wav_t>::iterator begin = actualwavs.begin();
			vector<wav_t>::iterator finish = actualwavs.end();
			vector<wav_t>::iterator found;
			// Add file to list only if it is an actual wav
			if ((found = std::find(begin, finish, f)) != finish) {
				f.filename = (*found).filename;
				// Also add plain wav as region
				std::vector<midi_ev_t> m;
				region_t r = {
					name,
					rindex,
					(int64_t)(start*ratefactor),
					(int64_t)(sampleoffset*ratefactor),
					(int64_t)(length*ratefactor),
					f,
					m
				};
				regions.push_back(r);
			// Region only
			} else {
				if (foundin(filename, string(".grp"))) {
					continue;
				}
				std::vector<midi_ev_t> m;
				region_t r = {
					name,
					rindex,
					(int64_t)(start*ratefactor),
					(int64_t)(sampleoffset*ratefactor),
					(int64_t)(length*ratefactor),
					f,
					m
				};
				regions.push_back(r);
			}
			rindex++;
		}
	}

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x03", 2)) {
		return;
	}
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2)) {
		return;
	}
	k++;

	//  Tracks
	uint32_t offset;
	uint32_t tracknumber = 0;
	uint32_t regionspertrack = 0;
	for (;k < len; k++) {
		if (	(ptfunxored[k  ] == 0x5a) &&
			(ptfunxored[k+1] == 0x04)) {
			break;
		}
		if (	(ptfunxored[k  ] == 0x5a) &&
			(ptfunxored[k+1] == 0x02)) {

			uint8_t lengthofname = 0;
			lengthofname = ptfunxored[k+9];
			if (lengthofname == 0x5a) {
				continue;
			}
			track_t tr;

			regionspertrack = (uint8_t)(ptfunxored[k+13+lengthofname]);

			//printf("regions/track=%d\n", regionspertrack);
			char name[256] = {0};
			for (j = 0; j < lengthofname; j++) {
				name[j] = ptfunxored[j+k+13];
			}
			name[j] = '\0';
			tr.name = string(name);
			tr.index = tracknumber++;

			for (j = k; regionspertrack > 0 && j < len; j++) {
				jumpto(&j, ptfunxored, len, (const unsigned char *)"\x5a\x07", 2);
				tr.reg.index = (uint16_t)(ptfunxored[j+11] & 0xff)
					| (uint16_t)((ptfunxored[j+12] << 8) & 0xff00);
				vector<region_t>::iterator begin = regions.begin();
				vector<region_t>::iterator finish = regions.end();
				vector<region_t>::iterator found;
				if ((found = std::find(begin, finish, tr.reg)) != finish) {
					tr.reg = (*found);
				}
				i = j+16;
				offset = u_endian_read4(&ptfunxored[i], is_bigendian);
				tr.reg.startpos = (int64_t)(offset*ratefactor);
				if (tr.reg.length > 0) {
					tracks.push_back(tr);
				}
				regionspertrack--;
			}
		}
	}
}

void
PTFFormat::parserest12(void) {
	uint32_t i,j,k,l,m,n;
	uint8_t startbytes = 0;
	uint8_t lengthbytes = 0;
	uint8_t offsetbytes = 0;
	uint8_t somethingbytes = 0;
	uint8_t skipbytes = 0;
	uint32_t maxregions = 0;
	uint32_t findex = 0;
	uint32_t findex2 = 0;
	uint32_t findex3 = 0;
	uint16_t rindex = 0;
	vector<region_t> groups;
	uint16_t groupcount, compoundcount, groupmax;
	uint16_t gindex, gindex2;

	m = 0;
	n = 0;
	vector<compound_t> groupmap;
	// Find region group total
	k = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"Custom 1\0\0\x5a", 11))
		goto nocustom;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\xff\xff\xff\xff", 4))
		return;

	if (!jumpback(&k, ptfunxored, len, (const unsigned char *)"\x5a", 1))
		return;

	jumpto(&k, ptfunxored, k+0x2000, (const unsigned char *)"\x5a\x03", 2);
	k++;

	groupcount = 0;
	for (i = k; i < len; i++) {
		if (!jumpto(&i, ptfunxored, len, (const unsigned char *)"\x5a\x03", 2))
			break;
		groupcount++;
	}
	verbose_printf("groupcount=%d\n", groupcount);

	// Find start of group names -> group indexes
	k = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"Custom 1\0\0\x5a", 11))
		return;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\xff\xff\xff\xff", 4))
		return;

	if (!jumpback(&k, ptfunxored, len, (const unsigned char *)"\x5a", 1))
		return;
	k++;

	// Skip total number of groups
	for (i = 0; i < groupcount; i++) {
		while (k < len) {
			if (		(ptfunxored[k  ] == 0x5a) &&
					((ptfunxored[k+1] == 0x03) || (ptfunxored[k+1] == 0x0a))) {
				break;
			}
			k++;
		}
		k++;
	}

	while (k < len) {
		if (		(ptfunxored[k  ] == 0x5a) &&
				(ptfunxored[k+1] & 0x02)) {
			break;
		}
		k++;
	}
	k++;

	while (k < len) {
		if (		(ptfunxored[k  ] == 0x5a) &&
				(ptfunxored[k+1] & 0x02)) {
			break;
		}
		k++;
	}
	k++;

	verbose_printf("start of groups k=0x%x\n", k);
	// Loop over all groups and associate the compound index/name
	for (i = 0; i < groupcount; i++) {
		while (k < len) {
			if (		(ptfunxored[k  ] == 0x5a) &&
					(ptfunxored[k+1] & 0x02)) {
				break;
			}
			k++;
		}
		if (k > len)
			break;
		gindex = u_endian_read2(&ptfunxored[k+9], is_bigendian);
		gindex2 = u_endian_read2(&ptfunxored[k+3], is_bigendian);

		uint8_t lengthofname = ptfunxored[k+13];
		char name[256] = {0};
		for (l = 0; l < lengthofname; l++) {
			name[l] = ptfunxored[k+17+l];
		}
		name[l] = '\0';

		if (strlen(name) == 0) {
			i--;
			k++;
			continue;
		}
		compound_t c = {
			(uint16_t)i,	// curr_index
			gindex,		// unknown1
			0,		// level
			0,		// ontopof_index
			gindex2,	// next_index
			string(name)
		};
		groupmap.push_back(c);
		k++;
	}

	// Sort lookup table by group index
	//std::sort(glookup.begin(), glookup.end(), regidx_compare);

	// print compounds as flattened tree
	j = 0;
	for (std::vector<compound_t>::iterator i = groupmap.begin(); i != groupmap.end(); ++i) {
		verbose_printf("g(%u) uk(%u) ni(%u) %s\n", i->curr_index, i->unknown1, i->next_index, i->name.c_str());
		j++;
	}

nocustom:
	// Find region groups
	k = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"Snap", 4))
		return;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x06", 2))
		return;
	k++;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16))
		return;
	k++;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16))
		return;
	k++;

	// Hack to find actual start of region group information
	while (k < len) {
		if ((ptfunxored[k+13] == 0x5a) && (ptfunxored[k+14] & 0xf)) {
			k += 13;
			continue;
		} else {
			if ((ptfunxored[k+9] == 0x5a) && (ptfunxored[k+10] & 0xf)) {
				k += 9;
				continue;
			}
		}
		if ((ptfunxored[k] == 0x5a) && (ptfunxored[k+1] & 0xf))
			break;
		k++;
	}
	verbose_printf("hack region groups k=0x%x\n", k);

	compoundcount = 0;
	j = k;
	groupmax = groupcount == 0 ? 0 : u_endian_read2(&ptfunxored[j+3], is_bigendian);
	groupcount = 0;
	for (i = k; (groupcount < groupmax) && (i < len-70); i++) {
		if (		(ptfunxored[i  ] == 0x5a) &&
				(ptfunxored[i+1] == 0x03)) {
				break;
		}
		if (		(ptfunxored[i  ] == 0x5a) &&
				((ptfunxored[i+1] == 0x01) || (ptfunxored[i+1] == 0x02))) {

			//findex = ptfunxored[i-48] | ptfunxored[i-47] << 8;
			//rindex = ptfunxored[i+3] | ptfunxored[i+4] << 8;

			uint8_t lengthofname = ptfunxored[i+9];
			if (ptfunxored[i+13] == 0x5a) {
				continue;
			}
			char name[256] = {0};
			for (j = 0; j < lengthofname; j++) {
				name[j] = ptfunxored[i+13+j];
			}
			name[j] = '\0';
			j += i+13;

			offsetbytes = (ptfunxored[j+1] & 0xf0) >> 4;
			lengthbytes = (ptfunxored[j+2] & 0xf0) >> 4;
			startbytes = (ptfunxored[j+3] & 0xf0) >> 4;
			somethingbytes = (ptfunxored[j+3] & 0xf);
			skipbytes = ptfunxored[j+4];
			uint16_t regionsingroup = ptfunxored[j+5
                                        +startbytes
                                        +lengthbytes
                                        +offsetbytes
                                        +somethingbytes
                                        +skipbytes
                                        +12]
				| ptfunxored[j+5
                                        +startbytes
                                        +lengthbytes
                                        +offsetbytes
                                        +somethingbytes
                                        +skipbytes
                                        +13] << 8;

			findex = ptfunxored[j+5
					+startbytes
					+lengthbytes
					+offsetbytes
					+somethingbytes
					+skipbytes
					+37]
				| ptfunxored[j+5
					+startbytes
					+lengthbytes
					+offsetbytes
					+somethingbytes
					+skipbytes
					+38] << 8;

			uint64_t sampleoffset = 0;
			switch (offsetbytes) {
			case 5:
				sampleoffset = u_endian_read5(&ptfunxored[j+5], false);
				break;
			case 4:
				sampleoffset = (uint64_t)u_endian_read4(&ptfunxored[j+5], false);
				break;
			case 3:
				sampleoffset = (uint64_t)u_endian_read3(&ptfunxored[j+5], false);
				break;
			case 2:
				sampleoffset = (uint64_t)u_endian_read2(&ptfunxored[j+5], false);
				break;
			case 1:
				sampleoffset = (uint64_t)(ptfunxored[j+5]);
				break;
			default:
				break;
			}
			j+=offsetbytes;
			uint64_t length = 0;
			switch (lengthbytes) {
			case 5:
				length = u_endian_read5(&ptfunxored[j+5], false);
				break;
			case 4:
				length = (uint64_t)u_endian_read4(&ptfunxored[j+5], false);
				break;
			case 3:
				length = (uint64_t)u_endian_read3(&ptfunxored[j+5], false);
				break;
			case 2:
				length = (uint64_t)u_endian_read2(&ptfunxored[j+5], false);
				break;
			case 1:
				length = (uint64_t)(ptfunxored[j+5]);
				break;
			default:
				break;
			}
			j+=lengthbytes;
			uint64_t start = 0;
			switch (startbytes) {
			case 5:
				start = u_endian_read5(&ptfunxored[j+5], false);
				break;
			case 4:
				start = (uint64_t)u_endian_read4(&ptfunxored[j+5], false);
				break;
			case 3:
				start = (uint64_t)u_endian_read3(&ptfunxored[j+5], false);
				break;
			case 2:
				start = (uint64_t)u_endian_read2(&ptfunxored[j+5], false);
				break;
			case 1:
				start = (uint64_t)(ptfunxored[j+5]);
				break;
			default:
				break;
			}
			j+=startbytes;

			if (offsetbytes == 5)
				sampleoffset -= 1000000000000ULL;
			if (startbytes == 5)
				start -= 1000000000000ULL;

			std::string filename = string(name);
			wav_t f = {
				filename,
				(uint16_t)findex,
				(int64_t)(start*ratefactor),
				(int64_t)(length*ratefactor),
			};

			if (strlen(name) == 0) {
				continue;
			}
			if (length == 0) {
				continue;
			}
			//if (foundin(filename, string(".grp")) && !regionsingroup && !findex) {
			//	// Empty region group
			//	verbose_printf("        EMPTY: %s\n", name);
			//	continue;
			if (regionsingroup) {
				// Active region grouping
				// Iterate parsing all the regions in the group
				verbose_printf("\nGROUP\t%d %s\n", groupcount, name);
				m = j;
				n = j+16;

				for (l = 0; l < regionsingroup; l++) {
					if (!jumpto(&n, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2)) {
						return;
					}
					n++;
				}
				n--;
				//printf("n=0x%x\n", n+112);
				//findex = ptfunxored[n+112] | (ptfunxored[n+113] << 8);
				findex = u_endian_read2(&ptfunxored[i-11], is_bigendian);
				findex2 = u_endian_read2(&ptfunxored[n+108], is_bigendian);
				//findex2= rindex; //XXX
				// Find wav with correct findex
				vector<wav_t>::iterator wave = actualwavs.end();
				for (vector<wav_t>::iterator aw = actualwavs.begin();
						aw != actualwavs.end(); ++aw) {
					if (aw->index == findex) {
						wave = aw;
					}
				}
				if (wave == actualwavs.end())
					return;

				if (!jumpto(&n, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2))
					return;
				n += 37;
				//rindex = ptfunxored[n] | (ptfunxored[n+1] << 8);
				for (l = 0; l < regionsingroup; l++) {
					if (!jumpto(&m, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2))
						return;

					m += 37;
					rindex = u_endian_read2(&ptfunxored[m], is_bigendian);

					m += 12;
					sampleoffset = 0;
					switch (offsetbytes) {
					case 5:
						sampleoffset = u_endian_read5(&ptfunxored[m], false);
						break;
					case 4:
						sampleoffset = (uint64_t)u_endian_read4(&ptfunxored[m], false);
						break;
					case 3:
						sampleoffset = (uint64_t)u_endian_read3(&ptfunxored[m], false);
						break;
					case 2:
						sampleoffset = (uint64_t)u_endian_read2(&ptfunxored[m], false);
						break;
					case 1:
						sampleoffset = (uint64_t)(ptfunxored[m]);
						break;
					default:
						break;
					}
					m+=offsetbytes+3;
					start = 0;
					switch (offsetbytes) {
					case 5:
						start = u_endian_read5(&ptfunxored[m], false);
						break;
					case 4:
						start = (uint64_t)u_endian_read4(&ptfunxored[m], false);
						break;
					case 3:
						start = (uint64_t)u_endian_read3(&ptfunxored[m], false);
						break;
					case 2:
						start = (uint64_t)u_endian_read2(&ptfunxored[m], false);
						break;
					case 1:
						start = (uint64_t)(ptfunxored[m]);
						break;
					default:
						break;
					}
					m+=offsetbytes+3;
					length = 0;
					switch (lengthbytes) {
					case 5:
						length = u_endian_read5(&ptfunxored[m], false);
						break;
					case 4:
						length = (uint64_t)u_endian_read4(&ptfunxored[m], false);
						break;
					case 3:
						length = (uint64_t)u_endian_read3(&ptfunxored[m], false);
						break;
					case 2:
						length = (uint64_t)u_endian_read2(&ptfunxored[m], false);
						break;
					case 1:
						length = (uint64_t)(ptfunxored[m]);
						break;
					default:
						break;
					}
					m+=8;
					findex3 = ptfunxored[m] | (ptfunxored[m+1] << 8);
					sampleoffset -= 1000000000000ULL;
					start -= 1000000000000ULL;

					/*
					// Find wav with correct findex
					vector<wav_t>::iterator wave = actualwavs.end();
					for (vector<wav_t>::iterator aw = actualwavs.begin();
							aw != actualwavs.end(); ++aw) {
						if (aw->index == (glookup.begin()+findex2)->startpos) {
							wave = aw;
						}
					}
					if (wave == actualwavs.end())
						return;
					// findex is the true source
					std::vector<midi_ev_t> md;
					region_t r = {
						name,
						(uint16_t)rindex,
						(int64_t)findex, //(start*ratefactor),
						(int64_t)findex2, //(sampleoffset*ratefactor),
						(int64_t)findex3, //(length*ratefactor),
						*wave,
						md
					};
					groups.push_back(r);
					*/
					vector<compound_t>::iterator g = groupmap.begin()+findex2;
					if (g >= groupmap.end())
						continue;
					compound_t c;
					c.name = string(g->name);
					c.curr_index = compoundcount;
					c.level = findex;
					c.ontopof_index = findex3;
					c.next_index = g->next_index;
					c.unknown1 = g->unknown1;
					compounds.push_back(c);
					verbose_printf("COMPOUND\tc(%d) %s (%d %d) -> c(%u) %s\n", c.curr_index, c.name.c_str(), c.level, c.ontopof_index, c.next_index, name);
					compoundcount++;
				}
				groupcount++;
			}
		}
	}
	j = 0;

	// Start pure regions
	k = m != 0 ? m : k - 1;
	if (!jumpto(&k, ptfunxored, k+64, (const unsigned char *)"\x5a\x05", 2))
		jumpto(&k, ptfunxored, k+0x400, (const unsigned char *)"\x5a\x02", 2);

	verbose_printf("pure regions k=0x%x\n", k);

	maxregions = u_endian_read4(&ptfunxored[k-4], is_bigendian);

	verbose_printf("maxregions=%u\n", maxregions);
	rindex = 0;
	for (i = k; rindex < maxregions && i < len; i++) {
		if (		(ptfunxored[i  ] == 0xff) &&
				(ptfunxored[i+1] == 0x5a) &&
				(ptfunxored[i+2] == 0x01)) {
			break;
		}
		//if (		(ptfunxored[i  ] == 0x5a) &&
		//		(ptfunxored[i+1] == 0x03)) {
		//	break;
		//}
		if (		(ptfunxored[i  ] == 0x5a) &&
				((ptfunxored[i+1] == 0x01) || (ptfunxored[i+1] == 0x02))) {

			//findex = ptfunxored[i-48] | ptfunxored[i-47] << 8;
			//rindex = ptfunxored[i+3] | ptfunxored[i+4] << 8;

			uint8_t lengthofname = ptfunxored[i+9];
			if (ptfunxored[i+13] == 0x5a) {
				continue;
			}
			char name[256] = {0};
			for (j = 0; j < lengthofname; j++) {
				name[j] = ptfunxored[i+13+j];
			}
			name[j] = '\0';
			j += i+13;

			offsetbytes = (ptfunxored[j+1] & 0xf0) >> 4;
			lengthbytes = (ptfunxored[j+2] & 0xf0) >> 4;
			startbytes = (ptfunxored[j+3] & 0xf0) >> 4;
			somethingbytes = (ptfunxored[j+3] & 0xf);
			skipbytes = ptfunxored[j+4];
			findex = ptfunxored[j+5
					+startbytes
					+lengthbytes
					+offsetbytes
					+somethingbytes
					+skipbytes
					+37]
				| ptfunxored[j+5
					+startbytes
					+lengthbytes
					+offsetbytes
					+somethingbytes
					+skipbytes
					+38] << 8;

			uint64_t sampleoffset = 0;
			switch (offsetbytes) {
			case 5:
				sampleoffset = u_endian_read5(&ptfunxored[j+5], false);
				break;
			case 4:
				sampleoffset = (uint64_t)u_endian_read4(&ptfunxored[j+5], false);
				break;
			case 3:
				sampleoffset = (uint64_t)u_endian_read3(&ptfunxored[j+5], false);
				break;
			case 2:
				sampleoffset = (uint64_t)u_endian_read2(&ptfunxored[j+5], false);
				break;
			case 1:
				sampleoffset = (uint64_t)(ptfunxored[j+5]);
				break;
			default:
				break;
			}
			j+=offsetbytes;
			uint64_t length = 0;
			switch (lengthbytes) {
			case 5:
				length = u_endian_read5(&ptfunxored[j+5], false);
				break;
			case 4:
				length = (uint64_t)u_endian_read4(&ptfunxored[j+5], false);
				break;
			case 3:
				length = (uint64_t)u_endian_read3(&ptfunxored[j+5], false);
				break;
			case 2:
				length = (uint64_t)u_endian_read2(&ptfunxored[j+5], false);
				break;
			case 1:
				length = (uint64_t)(ptfunxored[j+5]);
				break;
			default:
				break;
			}
			j+=lengthbytes;
			uint64_t start = 0;
			switch (startbytes) {
			case 5:
				start = u_endian_read5(&ptfunxored[j+5], false);
				break;
			case 4:
				start = (uint64_t)u_endian_read4(&ptfunxored[j+5], false);
				break;
			case 3:
				start = (uint64_t)u_endian_read3(&ptfunxored[j+5], false);
				break;
			case 2:
				start = (uint64_t)u_endian_read2(&ptfunxored[j+5], false);
				break;
			case 1:
				start = (uint64_t)(ptfunxored[j+5]);
				break;
			default:
				break;
			}
			j+=startbytes;

			if (offsetbytes == 5)
				sampleoffset -= 1000000000000ULL;
			if (startbytes == 5)
				start -= 1000000000000ULL;

			std::string filename = string(name);
			wav_t f = {
				filename,
				(uint16_t)findex,
				(int64_t)(start*ratefactor),
				(int64_t)(length*ratefactor),
			};

			if (strlen(name) == 0) {
				continue;
			}
			if (length == 0) {
				continue;
			}
			// Regular region mapping to a source
			uint32_t n = j;
			if (!jumpto(&n, ptfunxored, len, (const unsigned char *)"\x5a\x01", 2))
				return;
			//printf("XXX=%d\n", ptfunxored[n+12] | ptfunxored[n+13]<<8);

			// Find wav with correct findex
			vector<wav_t>::iterator wave = actualwavs.end();
			for (vector<wav_t>::iterator aw = actualwavs.begin();
					aw != actualwavs.end(); ++aw) {
				if (aw->index == findex) {
					wave = aw;
				}
			}
			if (wave == actualwavs.end()) {
				verbose_printf("missing source with findex\n");
				continue;
			}
			//verbose_printf("\n+r(%d) w(%d) REGION: %s st(%lx)x%u of(%lx)x%u ln(%lx)x%u\n", rindex, findex, name, start, startbytes, sampleoffset, offsetbytes, length, lengthbytes);
			verbose_printf("REGION\tg(NA)\tr(%d)\tw(%d) %s(%s)\n", rindex, findex, name, wave->filename.c_str());
			std::vector<midi_ev_t> md;
			region_t r = {
				name,
				rindex,
				(int64_t)(start*ratefactor),
				(int64_t)(sampleoffset*ratefactor),
				(int64_t)(length*ratefactor),
				*wave,
				md
			};
			regions.push_back(r);
			rindex++;
		}
	}

	// print compounds
	vector<uint16_t> rootnodes;
	bool found = false;

	j = 0;
	for (vector<compound_t>::iterator cmp = compounds.begin();
			cmp != compounds.end(); ++cmp) {
		found = false;
		for (vector<compound_t>::iterator tmp = compounds.begin();
				tmp != compounds.end(); ++tmp) {
			if (tmp == cmp)
				continue;
			if (tmp->ontopof_index == cmp->curr_index)
				found = true;
		}
		// Collect a vector of all the root nodes (no others point to)
		if (!found)
			rootnodes.push_back(cmp->curr_index);
	}

	for (vector<uint16_t>::iterator rt = rootnodes.begin();
			rt != rootnodes.end(); ++rt) {
		vector<compound_t>::iterator cmp = compounds.begin()+(*rt);
		// Now we are at a root node, follow to leaf
		if (cmp >= compounds.end())
			continue;

		verbose_printf("----\n");

		for (; cmp < compounds.end() && cmp->curr_index != cmp->next_index;
				cmp = compounds.begin()+cmp->next_index) {

			// Find region
			vector<region_t>::iterator r = regions.end();
			for (vector<region_t>::iterator rs = regions.begin();
					rs != regions.end(); rs++) {
				if (rs->index == cmp->unknown1 + cmp->level) {
					r = rs;
				}
			}
			if (r == regions.end())
				continue;
			verbose_printf("\t->cidx(%u) pl(%u)+ridx(%u) cflags(0x%x) ?(%u) grp(%s) reg(%s)\n", cmp->curr_index, cmp->level, cmp->unknown1, cmp->ontopof_index, cmp->next_index, cmp->name.c_str(), r->name.c_str());
		}
		// Find region
		vector<region_t>::iterator r = regions.end();
		for (vector<region_t>::iterator rs = regions.begin();
				rs != regions.end(); rs++) {
			if (rs->index == cmp->unknown1 + cmp->level) {
				r = rs;
			}
		}
		if (r == regions.end())
			continue;
		verbose_printf("\tLEAF->cidx(%u) pl(%u)+ridx(%u) cflags(0x%x) ?(%u) grp(%s) reg(%s)\n", cmp->curr_index, cmp->level, cmp->unknown1, cmp->ontopof_index, cmp->next_index, cmp->name.c_str(), r->name.c_str());
	}

	// Start grouped regions

	// Print region groups mapped to sources
	for (vector<region_t>::iterator a = groups.begin(); a != groups.end(); ++a) {
		// Find wav with findex
		vector<wav_t>::iterator wav = audiofiles.end();
		for (vector<wav_t>::iterator ws = audiofiles.begin();
				ws != audiofiles.end(); ws++) {
			if (ws->index == a->startpos) {
				wav = ws;
			}
		}
		if (wav == audiofiles.end())
			continue;

		// Find wav with findex2
		vector<wav_t>::iterator wav2 = audiofiles.end();
		for (vector<wav_t>::iterator ws = audiofiles.begin();
				ws != audiofiles.end(); ws++) {
			if (ws->index == a->sampleoffset) {
				wav2 = ws;
			}
		}
		if (wav2 == audiofiles.end())
			continue;

		verbose_printf("Group: %s -> %s OR %s\n", a->name.c_str(), wav->filename.c_str(), wav2->filename.c_str());
	}

	//filter(regions);
	//resort(regions);

	//  Tracks
	uint32_t offset;
	uint32_t tracknumber = 0;
	uint32_t regionspertrack = 0;

	// Total tracks
	j = k;
	if (!jumpto(&j, ptfunxored, len, (const unsigned char *)"\x5a\x03\x00", 3))
		return;
	//maxtracks = u_endian_read4(&ptfunxored[j-4]);

	// Jump to start of region -> track mappings
	if (jumpto(&k, ptfunxored, k + regions.size() * 0x400, (const unsigned char *)"\x5a\x08", 2)) {
		if (!jumpback(&k, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2))
			return;
	} else if (jumpto(&k, ptfunxored, k + regions.size() * 0x400, (const unsigned char *)"\x5a\x0a", 2)) {
		if (!jumpback(&k, ptfunxored, len, (const unsigned char *)"\x5a\x01", 2))
			return;
	} else {
		return;
	}
	verbose_printf("tracks k=0x%x\n", k);

	for (;k < len; k++) {
		if (	(ptfunxored[k  ] == 0x5a) &&
			(ptfunxored[k+1] & 0x04)) {
			break;
		}
		if (	(ptfunxored[k  ] == 0x5a) &&
			(ptfunxored[k+1] & 0x02)) {

			uint8_t lengthofname = 0;
			lengthofname = ptfunxored[k+9];
			if (lengthofname == 0x5a) {
				continue;
			}
			track_t tr;

			regionspertrack = (uint8_t)(ptfunxored[k+13+lengthofname]);

			//printf("regions/track=%d\n", regionspertrack);
			char name[256] = {0};
			for (j = 0; j < lengthofname; j++) {
				name[j] = ptfunxored[j+k+13];
			}
			name[j] = '\0';
			tr.name = string(name);
			tr.index = tracknumber++;

			for (j = k+18+lengthofname; regionspertrack > 0 && j < len; j++) {
				jumpto(&j, ptfunxored, len, (const unsigned char *)"\x5a", 1);
				bool isgroup = ptfunxored[j+27] > 0;
				if (isgroup) {
					tr.reg.name = string("");
					tr.reg.length = 0;
					//tr.reg.index = 0xffff;
					verbose_printf("TRACK: t(%d) g(%d) G(%s) -> T(%s)\n",
						tracknumber, tr.reg.index, tr.reg.name.c_str(), tr.name.c_str());
				} else {
					tr.reg.index = ((uint16_t)(ptfunxored[j+11]) & 0xff)
						| (((uint16_t)(ptfunxored[j+12]) << 8) & 0xff00);
					vector<region_t>::iterator begin = regions.begin();
					vector<region_t>::iterator finish = regions.end();
					vector<region_t>::iterator found;
					if ((found = std::find(begin, finish, tr.reg)) != finish) {
						tr.reg = *found;
					}
					verbose_printf("TRACK: t(%d) r(%d) R(%s) -> T(%s)\n",
						tracknumber, tr.reg.index, tr.reg.name.c_str(), tr.name.c_str());
				}
				i = j+16;
				offset = u_endian_read4(&ptfunxored[i], is_bigendian);
				tr.reg.startpos = (int64_t)(offset*ratefactor);
				if (tr.reg.length > 0) {
					tracks.push_back(tr);
				}
				regionspertrack--;

				jumpto(&j, ptfunxored, len, (const unsigned char *)"\xff\xff\xff\xff\xff\xff\xff\xff", 8);
				j += 12;
			}
		}
	}
}

#endif
