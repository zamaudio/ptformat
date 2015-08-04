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
#ifndef PTFFORMAT_H
#define PTFFORMAT_H

#include <stdlib.h>
#include <string.h>
#include <string>
#include <cstring>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

static uint32_t baselut[16] = {
	0xaaaaaaaa,
	0xaa955555,
	0xa9554aaa,
	0xa552a955,
	0xb56ad5aa,
	0x95a95a95,
	0x94a5294a,
	0x9696b4b5,
	0xd2d25a5a,
	0xd24b6d25,
	0xdb6db6da,
	0xd9249b6d,
	0xc9b64d92,
	0xcd93264d,
	0xccd99b32,
	0xcccccccd
};

static uint32_t hiseed[8] = {
	0x00000000,
	0x00b00000,
	0x0b001000,
	0x010b0b00,
	0x10b0b010,
	0xb01b01b0,
	0xb101bb10,
	0xbbbb1110
};

static uint32_t hixor4[4] = { // simple bit-map
	0x000,
	0x0a0,
	0xa00,
	0x0aa,
};

class PTFFormat {
public:
	PTFFormat();
	~PTFFormat();

	static uint32_t swapbytes32 (uint32_t v) {
		uint32_t rv = 0;
		rv |= ((v >>  0) & 0xf) << 28;
		rv |= ((v >>  4) & 0xf) << 24;
		rv |= ((v >>  8) & 0xf) << 20;
		rv |= ((v >> 12) & 0xf) << 16;
		rv |= ((v >> 16) & 0xf) << 12;
		rv |= ((v >> 20) & 0xf) <<  8;
		rv |= ((v >> 24) & 0xf) <<  4;
		rv |= ((v >> 28) & 0xf) <<  0;
		return rv;
	}

	static uint32_t swapbytes16 (uint32_t v) {
		uint32_t rv = 0;
		rv |= ((v >>  0) & 0xf) << 12;
		rv |= ((v >>  4) & 0xf) <<  8;
		rv |= ((v >>  8) & 0xf) <<  4;
		rv |= ((v >> 12) & 0xf) <<  0;
		return rv;
	}

	static uint32_t mirror16 (uint32_t v) {
		assert (0 == (v & 0xffff0000));
		return (swapbytes16 (v) << 16) | v;
	}

	static uint32_t himap(int i) {
		if (i & 4) {
			return swapbytes16 (hixor4 [3 - (i & 3)] ^ 0xaaaa);
		} else {
			return hixor4 [i & 3];
		}
	}

	static uint64_t gen_secret (int i) {
		int iwrap = i & 0x7f; // wrap at 0x80;
		int idx; // mirror at 0x40;
		uint32_t xor_lo = 0; // 0x40 flag
		bool xorX = false; // mirror at 0x20

		if (iwrap & 0x40) {
			idx = 0x80 - iwrap;
			xor_lo = 0x1;
		} else {
			idx = iwrap;
		}

		int i16 = (idx >> 1) & 15;

		if (idx & 0x20) {
			i16 = 15 - i16;
			xorX = true;
		}

		uint32_t lo = baselut [i16];

		int i08 = i16 & 7;

		uint32_t xk;

		if (i16 & 0x08) {
			xk = hiseed  [7 - i08] ^ 1 ^ mirror16 (himap(i08));
		} else {
			xk = hiseed  [i08];
		}

		if (xorX) {
			lo ^= 0xaaaaaaab;
			xk ^= 1;
		}

		uint32_t hi = swapbytes32(lo) ^ swapbytes32(xk);

		return  ((uint64_t)hi << 32) | (lo ^ xor_lo);
	}


	/* Return values:	0            success
				-1           could not open file as ptf
	*/
	int load(std::string path);

	typedef struct wav {
		std::string filename;
		uint16_t    index;

		int64_t     posabsolute;
		int64_t     length;

		bool operator ==(const struct wav& other) {
			return (this->index == other.index);
		}

	} wav_t;

	typedef struct region {
		std::string name;
		uint16_t    index;
		int64_t     startpos;
		int64_t     sampleoffset;
		int64_t     length;
		wav_t       wave;

		bool operator ==(const struct region& other) {
			return (this->index == other.index);
		}
	} region_t;

	typedef struct track {
		std::string name;
		uint16_t    index;
		uint8_t     playlist;
		region_t    reg;

		bool operator ==(const struct track& other) {
			return (this->index == other.index);
		}
	} track_t;

	std::vector<wav_t> audiofiles;
	std::vector<region_t> regions;
	std::vector<track_t> tracks;

	static bool trackexistsin(std::vector<track_t> tr, uint16_t index) {
		std::vector<track_t>::iterator begin = tr.begin();
		std::vector<track_t>::iterator finish = tr.end();
		std::vector<track_t>::iterator found;

		track_t f = { std::string(""), index };

		if ((found = std::find(begin, finish, f)) != finish) {
			return true;
		}
		return false;
	}

	static bool regionexistsin(std::vector<region_t> reg, uint16_t index) {
		std::vector<region_t>::iterator begin = reg.begin();
		std::vector<region_t>::iterator finish = reg.end();
		std::vector<region_t>::iterator found;

		region_t r = { std::string(""), index };

		if ((found = std::find(begin, finish, r)) != finish) {
			return true;
		}
		return false;
	}

	static bool wavexistsin(std::vector<wav_t> wv, uint16_t index) {
		std::vector<wav_t>::iterator begin = wv.begin();
		std::vector<wav_t>::iterator finish = wv.end();
		std::vector<wav_t>::iterator found;

		wav_t w = { std::string(""), index };

		if ((found = std::find(begin, finish, w)) != finish) {
			return true;
		}
		return false;
	}

	uint32_t sessionrate;
	uint8_t version;

	unsigned char c0;
	unsigned char c1;
	unsigned char *ptfunxored;
	int len;

private:
	bool foundin(std::string haystack, std::string needle);
	void parse(void);
	void parse8header(void);
	void parse9header(void);
	void parserest(void);
	std::vector<wav_t> actualwavs;
};


#endif
