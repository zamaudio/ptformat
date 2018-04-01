/*
    Copyright (C) 2015  Damien Zammit
    Copyright (C) 2015  Robin Gareus

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <assert.h>

#include <glib/gstdio.h>

#include "ptfformat.h"

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
hexdump(uint8_t *data, int len)
{
	int i,j,end,step=16;

	for (i = 0; i < len; i += step) {
		printf("0x%02X: ", i);
		end = i + step;
		if (end > len) end = len;
		for (j = i; j < end; j++) {
			printf("0x%02X ", data[j]);
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

PTFFormat::PTFFormat() : version(0), product(NULL) {
}

PTFFormat::~PTFFormat() {
	if (ptfunxored) {
		free(ptfunxored);
	}
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
PTFFormat::load(std::string path, int64_t targetsr) {
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

	while( ((uintptr_t)data < data_end) && (success == false) ) {

		if (data[0] != 0x5a) {
			success = false;
			break;
		}

		seg_type = data[1];
		/* Skip segment header */
		data += 3;
		if (data[0] == 0 && data[1] == 0) {
			/* LE */
			seg_len = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
		} else {
			/* BE */
			seg_len = data[3] << 24 | data[2] << 16 | data[1] << 8 | data[0];
		}
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
	} else if (version == 10) {
		parse10header();
		setrates();
		if (sessionrate < 44100 || sessionrate > 192000)
		  return -1;
		parseaudio();
		parserest10();
		parsemidi();
	} else if (version == 11) {
		parse10header();
		setrates();
		if (sessionrate < 44100 || sessionrate > 192000)
		  return -1;
		parseaudio();
		parserest11();
		parsemidi();
	} else if (version == 12) {
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
PTFFormat::setrates(void) {
	ratefactor = 1.f;
	if (sessionrate != 0) {
		ratefactor = (float)targetrate / sessionrate;
	}
}

void
PTFFormat::parse5header(void) {
	uint32_t k;

	// Find session sample rate
	k = 0x100;

	jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x00\x02", 3);

	sessionrate = 0;
	sessionrate |= ptfunxored[k+12] << 16;
	sessionrate |= ptfunxored[k+13] << 8;
	sessionrate |= ptfunxored[k+14];
}

void
PTFFormat::parse7header(void) {
	uint32_t k;

	// Find session sample rate
	k = 0x100;

	jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x00\x05", 3);

	sessionrate = 0;
	sessionrate |= ptfunxored[k+12] << 16;
	sessionrate |= ptfunxored[k+13] << 8;
	sessionrate |= ptfunxored[k+14];
}

void
PTFFormat::parse8header(void) {
	uint32_t k;

	// Find session sample rate
	k = 0;

	jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x05", 2);

	sessionrate = 0;
	sessionrate |= ptfunxored[k+11];
	sessionrate |= ptfunxored[k+12] << 8;
	sessionrate |= ptfunxored[k+13] << 16;
}

void
PTFFormat::parse9header(void) {
	uint32_t k;

	// Find session sample rate
	k = 0x100;

	jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x06", 2);

	sessionrate = 0;
	sessionrate |= ptfunxored[k+11];
	sessionrate |= ptfunxored[k+12] << 8;
	sessionrate |= ptfunxored[k+13] << 16;
}

void
PTFFormat::parse10header(void) {
	uint32_t k;

	// Find session sample rate
	k = 0x100;

	jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x09", 2);

	sessionrate = 0;
	sessionrate |= ptfunxored[k+11];
	sessionrate |= ptfunxored[k+12] << 8;
	sessionrate |= ptfunxored[k+13] << 16;
}

void
PTFFormat::parserest5(void) {
	uint32_t i, j, k;
	uint64_t regionspertrack, lengthofname;
	uint64_t startbytes, lengthbytes, offsetbytes;
	uint16_t tracknumber = 0;
	uint16_t findex;
	uint16_t rindex;

	k = 0;
	for (i = 0; i < 5; i++) {
		jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x00\x03", 3);
		k++;
	}
	k--;

	for (i = 0; i < 2; i++) {
		jumpback(&k, ptfunxored, len, (const unsigned char *)"\x5a\x00\x01", 3);
		if (k)
			k--;
	}
	k++;

	rindex = 0;
	while (k < len) {
		if (		(ptfunxored[k  ] == 0xff) &&
				(ptfunxored[k+1] == 0xff)) {
			break;
		}
		jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x00\x01", 3);

		lengthofname = ptfunxored[k+12];
		if (ptfunxored[k+13] == 0x5a) {
			k++;
			break;
		}
		char name[256] = {0};
		for (j = 0; j < lengthofname; j++) {
			name[j] = ptfunxored[k+13+j];
		}
		name[j] = '\0';
		regionspertrack = ptfunxored[k+13+j+3];
		for (i = 0; i < regionspertrack; i++) {
			jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x00\x03", 3);
			j = k+16;
			startbytes = (ptfunxored[j+3] & 0xf0) >> 4;
			lengthbytes = (ptfunxored[j+2] & 0xf0) >> 4;
			offsetbytes = (ptfunxored[j+1] & 0xf0) >> 4;
			//somethingbytes = (ptfunxored[j+1] & 0xf);
			findex = ptfunxored[k+14];
			j--;
			uint32_t start = 0;
			switch (startbytes) {
			case 4:
				start |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				start |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				start |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				start |= (uint32_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=startbytes;
			uint32_t length = 0;
			switch (lengthbytes) {
			case 4:
				length |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				length |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				length |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				length |= (uint32_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=lengthbytes;
			uint32_t sampleoffset = 0;
			switch (offsetbytes) {
			case 4:
				sampleoffset |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				sampleoffset |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				sampleoffset |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				sampleoffset |= (uint32_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=offsetbytes;

			//printf("name=`%s` start=%04x length=%04x offset=%04x findex=%d\n", name,start,length,sampleoffset,findex);

			std::string filename = string(name);
			wav_t f = {
				filename,
				findex,
				(int64_t)(start*ratefactor),
				(int64_t)(length*ratefactor),
			};

			vector<wav_t>::iterator begin = audiofiles.begin();
			vector<wav_t>::iterator finish = audiofiles.end();
			vector<wav_t>::iterator found;
			// Add file to lists
			if ((found = std::find(begin, finish, f)) != finish) {
				f.filename = (*found).filename;
				std::vector<midi_ev_t> m;
				region_t r = {
					name,
					rindex,
					(int64_t)(start*ratefactor),
					(int64_t)(sampleoffset*ratefactor),
					(int64_t)(length*ratefactor),
					*found,
					m
				};
				regions.push_back(r);
				vector<track_t>::iterator ti;
				vector<track_t>::iterator bt = tracks.begin();
				vector<track_t>::iterator et = tracks.end();
				track_t tr = { name, 0, 0, r };
				if ((ti = std::find(bt, et, tr)) != et) {
					tracknumber = (*ti).index;
				} else {
					tracknumber = tracks.size() + 1;
				}
				track_t t = {
					name,
					(uint16_t)tracknumber,
					uint8_t(0),
					r
				};
				tracks.push_back(t);
			} else {
				std::vector<midi_ev_t> m;
				region_t r = {
					name,
					rindex,
					(int64_t)(start*ratefactor),
					(int64_t)(sampleoffset*ratefactor),
					(int64_t)(length*ratefactor),
					f,
					m,
				};
				regions.push_back(r);
				vector<track_t>::iterator ti;
				vector<track_t>::iterator bt = tracks.begin();
				vector<track_t>::iterator et = tracks.end();
				track_t tr = { name, 0, 0, r };
				if ((ti = std::find(bt, et, tr)) != et) {
					tracknumber = (*ti).index;
				} else {
					tracknumber = tracks.size() + 1;
				}
				track_t t = {
					name,
					(uint16_t)tracknumber,
					uint8_t(0),
					r
				};
				tracks.push_back(t);
			}
			rindex++;
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

	// Find end of wav file list
	k = 0;
	jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5f\x50\x35", 3);
	k++;
	jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5f\x50\x35", 3);

	// Find actual wav names
	uint16_t numberofwavs = ptfunxored[k-23];
	char wavname[256];
	i = k;
	jumpto(&i, ptfunxored, len, (const unsigned char *)"Files", 5);

	wavnumber = 0;
	i+=16;
	char ext[5];
	while (i < len && numberofwavs > 0) {
		i++;
		if (		(ptfunxored[i  ] == 0x5a) &&
				(ptfunxored[i+1] == 0x00) &&
				(ptfunxored[i+2] == 0x05)) {
			break;
		}
		lengthofname = ptfunxored[i];
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
		wav_t f = { wave, (uint16_t)(wavnumber++), 0, 0 };

		if (foundin(wave, string(".grp"))) {
			continue;
		}

		actualwavs.push_back(f);
		audiofiles.push_back(f);
		//printf("done\n");
		numberofwavs--;
		i += 7;
	}
	resort(actualwavs);
	resort(audiofiles);
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
		zero_ticks = (uint64_t)ptfunxored[k] |
			(uint64_t)ptfunxored[k+1] << 8 |
			(uint64_t)ptfunxored[k+2] << 16 |
			(uint64_t)ptfunxored[k+3] << 24 |
			(uint64_t)ptfunxored[k+4] << 32;
		for (i = 0; i < n_midi_events && k < len; i++, k += 35) {
			midi_pos = (uint64_t)ptfunxored[k] |
				(uint64_t)ptfunxored[k+1] << 8 |
				(uint64_t)ptfunxored[k+2] << 16 |
				(uint64_t)ptfunxored[k+3] << 24 |
				(uint64_t)ptfunxored[k+4] << 32;
			midi_pos -= zero_ticks;
			midi_note = ptfunxored[k+8];
			midi_len = (uint64_t)ptfunxored[k+9] |
				(uint64_t)ptfunxored[k+10] << 8 |
				(uint64_t)ptfunxored[k+11] << 16 |
				(uint64_t)ptfunxored[k+12] << 24 |
				(uint64_t)ptfunxored[k+13] << 32;
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

		nregions = 0;
		nregions |= ptfunxored[k];
		nregions |= ptfunxored[k+1] << 8;

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
				(int64_t)0xe8d4a51000ull,
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

	k -= 4;

	nmiditracks = 0;
	nmiditracks |= ptfunxored[k];
	nmiditracks |= ptfunxored[k+1] << 8;

	k += 4;

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
		nregions = 0;
		nregions |= ptfunxored[k];
		nregions |= ptfunxored[k+1] << 8;

		for (i = 0; (i < nregions) && (k < len); i++) {
			k += 24;

			ridx = 0;
			ridx |= ptfunxored[k];
			ridx |= ptfunxored[k+1] << 8;

			k += 5;

			region_pos = (uint64_t)ptfunxored[k] |
					(uint64_t)ptfunxored[k+1] << 8 |
					(uint64_t)ptfunxored[k+2] << 16 |
					(uint64_t)ptfunxored[k+3] << 24 |
					(uint64_t)ptfunxored[k+4] << 32;

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
				mtr.reg.startpos = std::labs(region_pos - mtr.reg.startpos);
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
		n_midi_events = ptfunxored[k] | ptfunxored[k+1] << 8 |
				ptfunxored[k+2] << 16 | ptfunxored[k+3] << 24;

		k += 4;
		zero_ticks = (uint64_t)ptfunxored[k] |
			(uint64_t)ptfunxored[k+1] << 8 |
			(uint64_t)ptfunxored[k+2] << 16 |
			(uint64_t)ptfunxored[k+3] << 24 |
			(uint64_t)ptfunxored[k+4] << 32;
		for (i = 0; i < n_midi_events && k < len; i++, k += 35) {
			midi_pos = (uint64_t)ptfunxored[k] |
				(uint64_t)ptfunxored[k+1] << 8 |
				(uint64_t)ptfunxored[k+2] << 16 |
				(uint64_t)ptfunxored[k+3] << 24 |
				(uint64_t)ptfunxored[k+4] << 32;
			midi_pos -= zero_ticks;
			midi_note = ptfunxored[k+8];
			midi_len = (uint64_t)ptfunxored[k+9] |
				(uint64_t)ptfunxored[k+10] << 8 |
				(uint64_t)ptfunxored[k+11] << 16 |
				(uint64_t)ptfunxored[k+12] << 24 |
				(uint64_t)ptfunxored[k+13] << 32;
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

		nregions = 0;
		nregions |= ptfunxored[k];
		nregions |= ptfunxored[k+1] << 8;

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

			ridx = ptfunxored[k];
			ridx |= ptfunxored[k+1] << 8;

			k += 3;
			struct mchunk mc = *(midichunks.begin()+ridx);

			wav_t w = { std::string(""), 0, 0, 0 };
			region_t r = {
				midiregionname,
				regionnumber++,
				//(int64_t)mc.zero,
				(int64_t)0xe8d4a51000ull,
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

	k -= 4;

	nmiditracks = 0;
	nmiditracks |= ptfunxored[k];
	nmiditracks |= ptfunxored[k+1] << 8;

	k += 4;

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
		nregions = 0;
		nregions |= ptfunxored[k];
		nregions |= ptfunxored[k+1] << 8;

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

			ridx = 0;
			ridx |= ptfunxored[k];
			ridx |= ptfunxored[k+1] << 8;

			k += 5;

			region_pos = (uint64_t)ptfunxored[k] |
					(uint64_t)ptfunxored[k+1] << 8 |
					(uint64_t)ptfunxored[k+2] << 16 |
					(uint64_t)ptfunxored[k+3] << 24 |
					(uint64_t)ptfunxored[k+4] << 32;

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
				mtr.reg.startpos = std::labs(region_pos - mtr.reg.startpos);
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

	numberofwavs = 0;
	numberofwavs |= (uint32_t)(ptfunxored[j-1] << 24);
	numberofwavs |= (uint32_t)(ptfunxored[j-2] << 16);
	numberofwavs |= (uint32_t)(ptfunxored[j-3] << 8);
	numberofwavs |= (uint32_t)(ptfunxored[j-4]);
	//printf("%d wavs\n", numberofwavs);

	// Find actual wav names
	char wavname[256];
	j = k;
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

		if (foundin(wave, string("Audio Files"))) {
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
				sampleoffset |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				sampleoffset |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				sampleoffset |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				sampleoffset |= (uint32_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=offsetbytes;
			uint32_t length = 0;
			switch (lengthbytes) {
			case 4:
				length |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				length |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				length |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				length |= (uint32_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=lengthbytes;
			uint32_t start = 0;
			switch (startbytes) {
			case 4:
				start |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				start |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				start |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				start |= (uint32_t)(ptfunxored[j+5]);
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
				audiofiles.push_back(f);
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
				offset = 0;
				offset |= (uint32_t)(ptfunxored[i+3] << 24);
				offset |= (uint32_t)(ptfunxored[i+2] << 16);
				offset |= (uint32_t)(ptfunxored[i+1] << 8);
				offset |= (uint32_t)(ptfunxored[i]);
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
PTFFormat::parserest10(void) {
	uint32_t i,j,k;
	// Find Regions
	uint8_t startbytes = 0;
	uint8_t lengthbytes = 0;
	uint8_t offsetbytes = 0;
	uint8_t somethingbytes = 0;
	uint8_t skipbytes = 0;
	uint16_t rindex = 0;
	uint32_t findex = 0;

	k = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"Snap", 4)) {
		return;
	}

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2)) {
		return;
	}

	k++;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2)) {
		return;
	}

	k++;

	for (i = k; i < len-70; i++) {
		if (		(ptfunxored[i  ] == 0x5a) &&
				(ptfunxored[i+1] == 0x08)) {
				break;
		}
		if (		(ptfunxored[i  ] == 0x5a) &&
				(ptfunxored[i+1] == 0x01)) {

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
			//uint8_t disabled = ptfunxored[j];
			//printf("%s\n", name);

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
					+37];
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
				sampleoffset |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				sampleoffset |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				sampleoffset |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				sampleoffset |= (uint32_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=offsetbytes;
			uint32_t length = 0;
			switch (lengthbytes) {
			case 4:
				length |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				length |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				length |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				length |= (uint32_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=lengthbytes;
			uint32_t start = 0;
			switch (startbytes) {
			case 4:
				start |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				start |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				start |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				start |= (uint32_t)(ptfunxored[j+5]);
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
				0,
				(int64_t)(start*ratefactor),
				(int64_t)(length*ratefactor),
			};

			if (strlen(name) == 0) {
				continue;
			}
			if (length == 0) {
				continue;
			}
			f.index = findex;
			//printf("something=%d\n", something);

			vector<wav_t>::iterator begin = actualwavs.begin();
			vector<wav_t>::iterator finish = actualwavs.end();
			vector<wav_t>::iterator found;
			// Add file to list only if it is an actual wav
			if ((found = std::find(begin, finish, f)) != finish) {
				f.filename = (*found).filename;
				audiofiles.push_back(f);
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
			//printf("%s\n", name);
		}
	}
	//  Tracks
	uint32_t offset;
	uint32_t tracknumber = 0;
	uint32_t regionspertrack = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x08", 2)) {
		return;
	}
	k++;
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
				jumpto(&j, ptfunxored, len, (const unsigned char *)"\x5a\x08", 2);
				tr.reg.index = (uint16_t)(ptfunxored[j+11] & 0xff)
					| (uint16_t)((ptfunxored[j+12] << 8) & 0xff00);
				vector<region_t>::iterator begin = regions.begin();
				vector<region_t>::iterator finish = regions.end();
				vector<region_t>::iterator found;
				if ((found = std::find(begin, finish, tr.reg)) != finish) {
					tr.reg = (*found);
				}
				i = j+16;
				offset = 0;
				offset |= (uint32_t)(ptfunxored[i+3] << 24);
				offset |= (uint32_t)(ptfunxored[i+2] << 16);
				offset |= (uint32_t)(ptfunxored[i+1] << 8);
				offset |= (uint32_t)(ptfunxored[i]);
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
PTFFormat::parserest11(void) {
	uint32_t i,j,k;
	// Find Regions
	uint8_t startbytes = 0;
	uint8_t lengthbytes = 0;
	uint8_t offsetbytes = 0;
	uint8_t somethingbytes = 0;
	uint8_t skipbytes = 0;
	uint16_t rindex = 0;
	uint32_t findex = 0;

	k = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"Snap", 4)) {
		return;
	}

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2)) {
		return;
	}

	k++;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2)) {
		return;
	}

	k++;

	for (i = k; i < len-70; i++) {
		if (		(ptfunxored[i  ] == 0x5a) &&
				(ptfunxored[i+1] == 0x07)) {
				break;
		}
		if (		(ptfunxored[i  ] == 0x5a) &&
				(ptfunxored[i+1] == 0x01)) {

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
			uint8_t disabled = ptfunxored[j];
			if (disabled) {
				rindex++;
				continue;
			}
			//printf("%s\n", name);

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
					+37];
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
				sampleoffset |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				sampleoffset |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				sampleoffset |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				sampleoffset |= (uint32_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=offsetbytes;
			uint32_t length = 0;
			switch (lengthbytes) {
			case 4:
				length |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				length |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				length |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				length |= (uint32_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=lengthbytes;
			uint32_t start = 0;
			switch (startbytes) {
			case 4:
				start |= (uint32_t)(ptfunxored[j+8] << 24);
			case 3:
				start |= (uint32_t)(ptfunxored[j+7] << 16);
			case 2:
				start |= (uint32_t)(ptfunxored[j+6] << 8);
			case 1:
				start |= (uint32_t)(ptfunxored[j+5]);
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

			if (strlen(name) == 0) {
				continue;
			}
			if (length == 0) {
				continue;
			}
			//printf("something=%d\n", something);

			vector<wav_t>::iterator begin = actualwavs.begin();
			vector<wav_t>::iterator finish = actualwavs.end();
			vector<wav_t>::iterator found;
			// Add file to list only if it is an actual wav
			if ((found = std::find(begin, finish, f)) != finish) {
				f.filename = (*found).filename;
				audiofiles.push_back(f);
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
			//printf("%s\n", name);
		}
	}
	//  Tracks
	uint32_t offset;
	uint32_t tracknumber = 0;
	uint32_t regionspertrack = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\xff\xff\xff\xff", 4)) {
		return;
	}
	for (;k < len; k++) {
		if (	(ptfunxored[k  ] == 0x5a) &&
			(ptfunxored[k+1] == 0x03)) {
			break;
		}
		if (	(ptfunxored[k  ] == 0x5a) &&
			(ptfunxored[k+1] == 0x02)) {

			track_t tr;
			uint8_t lengthofname = 0;
			lengthofname = ptfunxored[k+9];
			if (lengthofname == 0x5a) {
				continue;
			}

			regionspertrack = (uint8_t)(ptfunxored[k+13+lengthofname]);
			if (regionspertrack == 0) {
				continue;
			}

			//printf("regions/track=%d\n", regionspertrack);
			char name[256] = {0};
			for (j = 0; j < lengthofname; j++) {
				name[j] = ptfunxored[j+k+13];
			}
			name[j] = '\0';
			tr.name = string(name);
			tr.index = tracknumber++;
			// Hack
			if (strlen(name) == 0) {
				break;
			}
			for (j = k; regionspertrack > 0 && j < len; j++) {
				jumpto(&j, ptfunxored, len, (const unsigned char *)"\x5a\x08", 2);
				tr.reg.index = (uint16_t)(ptfunxored[j+11] & 0xff)
					| (uint16_t)((ptfunxored[j+12] << 8) & 0xff00);
				vector<region_t>::iterator begin = regions.begin();
				vector<region_t>::iterator finish = regions.end();
				vector<region_t>::iterator found;
				if ((found = std::find(begin, finish, tr.reg)) != finish) {
					tr.reg = (*found);
				}
				i = j+16;
				offset = 0;
				offset |= (uint32_t)(ptfunxored[i+3] << 24);
				offset |= (uint32_t)(ptfunxored[i+2] << 16);
				offset |= (uint32_t)(ptfunxored[i+1] << 8);
				offset |= (uint32_t)(ptfunxored[i]);
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
	uint32_t i,j,k,l;
	uint8_t startbytes = 0;
	uint8_t lengthbytes = 0;
	uint8_t offsetbytes = 0;
	uint8_t somethingbytes = 0;
	uint8_t skipbytes = 0;
	uint32_t findex = 0;
	uint32_t findex2 = 0;
	uint32_t findex3 = 0;
	uint16_t rindex = 0;
	vector<region_t> groups;
	uint16_t groupcount, groupmax;
/*
	vector<wav_t> groupsrcs;

	// Find region group information
	k = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"Custom 1\0\0\x5a", 11))
		return;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\xff\xff\xff\xff", 4))
		return;

	if (!jumpback(&k, ptfunxored, len, (const unsigned char *)"\x5a\x08", 2))
		return;

	uint16_t gwindex;
	uint16_t gindex;

	j = 0;
	for (i = k; i < len-70; i++) {
		if (		(ptfunxored[i  ] == 0x5a) &&
				(ptfunxored[i+1] == 0x08)) {
			//gindex = ptfunxored[i-24] | ptfunxored[i-23] << 8;
			gwindex = ptfunxored[i+11] | ptfunxored[i+12] << 8;
			//gindex = ptfunxored[i+42] | ptfunxored[i+43] << 8;
			//gwindex = ptfunxored[i+9] | ptfunxored[i+10] << 8;

			// Find wav with correct findex
			vector<wav_t>::iterator wave = actualwavs.end();
			for (vector<wav_t>::iterator aw = actualwavs.begin();
					aw != actualwavs.end(); ++aw) {
				if (aw->index == gwindex) {
					wave = aw;
				}
			}
			if (wave == actualwavs.end())
				continue;

			groupsrcs.push_back(*wave);
			j++;
		}
	}


	// Find start of group names -> group indexes
	k = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"Custom 1\0\0\x5a", 11))
		return;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\xff\xff\xff\xff", 4))
		return;

	if (!jumpback(&k, ptfunxored, len, (const unsigned char *)"\x5a\x08", 2))
		return;
	k++;

	// Total number of groups = j
	for (i = 0; i < j/2; i++) {
		if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x03", 2))
			return;
		k++;
	}

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2))
		return;
	k++;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2))
		return;
	k++;

	vector<region_t> glookup;

	// Loop over all groups and associate its group index to the group name
	for (i = 0; i < j/2; i++) {
		if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2))
			return;
		gindex = ptfunxored[k+9] | ptfunxored[k+10] << 8;
		//gindex = ptfunxored[k+3] | ptfunxored[k+4] << 8;

		uint8_t lengthofname = ptfunxored[k+13];
		char name[256] = {0};
		for (l = 0; l < lengthofname; l++) {
			name[l] = ptfunxored[k+17+l];
		}
		name[l] = '\0';

		// dummy region to hold gindex and group name
		wav_t w = { std::string(""), 0, 0, 0 };
		std::vector<midi_ev_t> md;
		region_t g = {
			string(name),
			gindex,
			0,
			0,
			0,
			w,
			md
		};

		glookup.push_back(g);
		k++;
	}

	// Sort lookup table by group index
	std::sort(glookup.begin(), glookup.end(), regidx_compare);

	// print grouped regions
	j = 0;
	for (std::vector<wav_t>::iterator i = groupsrcs.begin(); i != groupsrcs.end(); ++i) {
		printf("g(%d) w(%d)(%d) %s\n", j, i->index, (glookup.begin()+j)->index, (glookup.begin()+j)->name.c_str());
		j++;
	}
*/
	// Find region groups
	k = 0;
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"Snap", 4))
		return;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\xff\xff\xff\xff", 4))
		return;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x01", 2))
		return;
	k++;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x01", 2))
		return;
	k++;

	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x01", 2))
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
	verbose_printf("k=0x%x\n", k);

	groupcount = 0;
	j = k;
	if (!jumpto(&j, ptfunxored, j+100, (const unsigned char *)"\x5a\x09", 2)) {
		groupmax = 0;
	} else {
		groupmax = ptfunxored[j+3] | ptfunxored[j+4] << 8;
	}

	i = k;
	for (; (groupcount < groupmax) && (i < len-70); i++) {
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
				sampleoffset |= (uint64_t)(ptfunxored[j+9]) << 32;
			case 4:
				sampleoffset |= (uint64_t)(ptfunxored[j+8]) << 24;
			case 3:
				sampleoffset |= (uint64_t)(ptfunxored[j+7]) << 16;
			case 2:
				sampleoffset |= (uint64_t)(ptfunxored[j+6]) << 8;
			case 1:
				sampleoffset |= (uint64_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=offsetbytes;
			uint64_t length = 0;
			switch (lengthbytes) {
			case 5:
				length |= (uint64_t)(ptfunxored[j+9]) << 32;
			case 4:
				length |= (uint64_t)(ptfunxored[j+8]) << 24;
			case 3:
				length |= (uint64_t)(ptfunxored[j+7]) << 16;
			case 2:
				length |= (uint64_t)(ptfunxored[j+6]) << 8;
			case 1:
				length |= (uint64_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=lengthbytes;
			uint64_t start = 0;
			switch (startbytes) {
			case 5:
				start |= (uint64_t)(ptfunxored[j+9]) << 32;
			case 4:
				start |= (uint64_t)(ptfunxored[j+8]) << 24;
			case 3:
				start |= (uint64_t)(ptfunxored[j+7]) << 16;
			case 2:
				start |= (uint64_t)(ptfunxored[j+6]) << 8;
			case 1:
				start |= (uint64_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=startbytes;

			if (offsetbytes == 5)
				sampleoffset -= 1000000000000ul;
			if (startbytes == 5)
				start -= 1000000000000ul;

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
				verbose_printf("\nGROUP\tg(%d) %s\n", groupcount, name);
				uint32_t m = j;
				uint32_t n = j+16;

				for (l = 0; l < regionsingroup; l++) {
					if (!jumpto(&n, ptfunxored, len, (const unsigned char *)"\x5a\x02", 2)) {
						return;
					}
					n++;
				}
				n--;
				//printf("n=0x%x\n", n+112);
				//findex = ptfunxored[n+112] | (ptfunxored[n+113] << 8);
				findex = ptfunxored[i-11] | ptfunxored[i-10] << 8;
				findex2 = ptfunxored[n+108] | (ptfunxored[n+109] << 8);
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
					rindex = ptfunxored[m] | (ptfunxored[m+1] << 8);

					m += 12;
					sampleoffset = 0;
					switch (offsetbytes) {
					case 5:
						sampleoffset |= (uint64_t)(ptfunxored[m+4]) << 32;
					case 4:
						sampleoffset |= (uint64_t)(ptfunxored[m+3]) << 24;
					case 3:
						sampleoffset |= (uint64_t)(ptfunxored[m+2]) << 16;
					case 2:
						sampleoffset |= (uint64_t)(ptfunxored[m+1]) << 8;
					case 1:
						sampleoffset |= (uint64_t)(ptfunxored[m]);
					default:
						break;
					}
					m+=offsetbytes+3;
					start = 0;
					switch (offsetbytes) {
					case 5:
						start |= (uint64_t)(ptfunxored[m+4]) << 32;
					case 4:
						start |= (uint64_t)(ptfunxored[m+3]) << 24;
					case 3:
						start |= (uint64_t)(ptfunxored[m+2]) << 16;
					case 2:
						start |= (uint64_t)(ptfunxored[m+1]) << 8;
					case 1:
						start |= (uint64_t)(ptfunxored[m]);
					default:
						break;
					}
					m+=offsetbytes+3;
					length = 0;
					switch (lengthbytes) {
					case 5:
						length |= (uint64_t)(ptfunxored[m+4]) << 32;
					case 4:
						length |= (uint64_t)(ptfunxored[m+3]) << 24;
					case 3:
						length |= (uint64_t)(ptfunxored[m+2]) << 16;
					case 2:
						length |= (uint64_t)(ptfunxored[m+1]) << 8;
					case 1:
						length |= (uint64_t)(ptfunxored[m]);
					default:
						break;
					}
					m+=8;
					findex3 = ptfunxored[m] | (ptfunxored[m+1] << 8);
					sampleoffset -= 1000000000000ul;
					start -= 1000000000000ul;

					if (findex3 == 0xffff) {
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

						//verbose_printf("r(%d) ?w(%d)(%d) -----: %s st(%lu) of(%lu) ln(%lu)\n", rindex, findex, findex2, wave->filename.c_str(), start, sampleoffset, length); 
						verbose_printf("?\t%d %d %d %d\n", rindex, findex, findex2, findex3); 
					} else {
						/*
						// find region with ridx=findex3
						vector<region_t>::iterator rlookup = regions.end();
						for (vector<region_t>::iterator rs = regions.begin();
								rs != regions.end(); ++rs) {
							if (rs->index == findex3) {
								rlookup = rs;
							}
						}
						if (rlookup == regions.end()) {
							printf("????\n");
							return;
						}
						*/
						wav_t w = { std::string(""), 0, 0, 0 };
						std::vector<midi_ev_t> md;
						region_t r = {
							name,
							(uint16_t)rindex,
							(int64_t)findex, //(start*ratefactor),
							(int64_t)findex2, //(sampleoffset*ratefactor),
							(int64_t)findex3, //(length*ratefactor),
							w,
							md
						};
						groups.push_back(r);
						verbose_printf("?\t%d %d %d %d\n", rindex, findex, findex2, findex3);
					}
				}
				groupcount++;
			}
		}
	}

	k = i;
	// Start pure regions
	verbose_printf("k=%x\n", k);
	rindex = 0;
	for (i = k; i < len-70; i++) {
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
				sampleoffset |= (uint64_t)(ptfunxored[j+9]) << 32;
			case 4:
				sampleoffset |= (uint64_t)(ptfunxored[j+8]) << 24;
			case 3:
				sampleoffset |= (uint64_t)(ptfunxored[j+7]) << 16;
			case 2:
				sampleoffset |= (uint64_t)(ptfunxored[j+6]) << 8;
			case 1:
				sampleoffset |= (uint64_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=offsetbytes;
			uint64_t length = 0;
			switch (lengthbytes) {
			case 5:
				length |= (uint64_t)(ptfunxored[j+9]) << 32;
			case 4:
				length |= (uint64_t)(ptfunxored[j+8]) << 24;
			case 3:
				length |= (uint64_t)(ptfunxored[j+7]) << 16;
			case 2:
				length |= (uint64_t)(ptfunxored[j+6]) << 8;
			case 1:
				length |= (uint64_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=lengthbytes;
			uint64_t start = 0;
			switch (startbytes) {
			case 5:
				start |= (uint64_t)(ptfunxored[j+9]) << 32;
			case 4:
				start |= (uint64_t)(ptfunxored[j+8]) << 24;
			case 3:
				start |= (uint64_t)(ptfunxored[j+7]) << 16;
			case 2:
				start |= (uint64_t)(ptfunxored[j+6]) << 8;
			case 1:
				start |= (uint64_t)(ptfunxored[j+5]);
			default:
				break;
			}
			j+=startbytes;

			if (offsetbytes == 5)
				sampleoffset -= 1000000000000ul;
			if (startbytes == 5)
				start -= 1000000000000ul;

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
				printf("wtf missing source with findex\n");
				return;
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
	if (!jumpto(&k, ptfunxored, len, (const unsigned char *)"\x5a\x03\x00", 3)) {
		return;
	}
	for (;k < len; k++) {
		if (	(ptfunxored[k  ] == 0x5a) &&
			(ptfunxored[k+1] & 0x04)) {
			break;
		}
		if (	(ptfunxored[k  ] == 0x5a) &&
			(ptfunxored[k+1] == 0x03)) {

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
				tr.reg.index = ((uint16_t)(ptfunxored[j+11]) & 0xff)
					| (((uint16_t)(ptfunxored[j+12]) << 8) & 0xff00);
				vector<region_t>::iterator begin = regions.begin();
				vector<region_t>::iterator finish = regions.end();
				vector<region_t>::iterator found;
				if ((found = std::find(begin, finish, tr.reg)) != finish) {
					tr.reg = (*found);
				}
				i = j+16;
				offset = 0;
				offset |= (uint32_t)(ptfunxored[i+3] << 24);
				offset |= (uint32_t)(ptfunxored[i+2] << 16);
				offset |= (uint32_t)(ptfunxored[i+1] << 8);
				offset |= (uint32_t)(ptfunxored[i]);
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
