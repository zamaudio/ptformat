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
#ifndef PTFFORMAT_H
#define PTFFORMAT_H

#include <string>
#include <cstring>
#include <algorithm>
#include <vector>
#include <stdint.h>
#include "ptformat/visibility.h"

#define BITCODE			"0010111100101011"
#define ZMARK			'\x5a'
#define ZERO_TICKS		0xe8d4a51000ULL
#define MAX_CONTENT_TYPE	0x3000
#define MAX_CHANNELS_PER_TRACK	8

class LIBPTFORMAT_API PTFFormat {
public:
	PTFFormat();
	~PTFFormat();

	/* Return values:	0            success
				-1           could not parse pt session
	*/
	int load(std::string path, int64_t targetsr);

	/* Return values:	0            success
				-1           could not decrypt pt session
	*/
	int unxor(std::string path);

	struct wav_t {
		std::string filename;
		uint16_t    index;

		int64_t     posabsolute;
		int64_t     length;

		bool operator <(const struct wav_t& other) const {
			return (strcasecmp(this->filename.c_str(),
					other.filename.c_str()) < 0);
		}

		bool operator ==(const struct wav_t& other) const {
			return (this->filename == other.filename ||
				this->index == other.index);
		}

	};

	struct midi_ev_t {
		uint64_t pos;
		uint64_t length;
		uint8_t note;
		uint8_t velocity;
	};

	typedef struct region {
		std::string name;
		uint16_t    index;
		int64_t     startpos;
		int64_t     sampleoffset;
		int64_t     length;
		wav_t       wave;
		std::vector<midi_ev_t> midi;

		bool operator ==(const struct region& other) {
			return (this->index == other.index);
		}

		bool operator <(const struct region& other) const {
			return (strcasecmp(this->name.c_str(),
					other.name.c_str()) < 0);
		}
	} region_t;

	typedef struct track {
		std::string name;
		uint16_t    index;
		uint8_t     playlist;
		region_t    reg;

		bool operator <(const struct track& other) const {
			return (this->index < other.index);
		}

		bool operator ==(const struct track& other) {
			return (this->index == other.index);
		}
	} track_t;

	bool find_track(uint16_t index, std::vector<track_t>::iterator& ti) {
		std::vector<track_t>::iterator begin = tracks.begin();
		std::vector<track_t>::iterator finish = tracks.end();
		std::vector<track_t>::iterator found;

		// Create dummy track with index
		wav_t w = { std::string(""), 0, 0, 0 };
		std::vector<midi_ev_t> m;
		region_t r = { std::string(""), 0, 0, 0, 0, w, m};
		track_t t = { std::string(""), index, 0, r};

		if ((found = std::find(begin, finish, t)) != finish) {
			ti = found;
			return true;
		}
		return false;
	}

	bool find_region(uint16_t index, std::vector<region_t>::iterator& ri) {
		std::vector<region_t>::iterator begin = regions.begin();
		std::vector<region_t>::iterator finish = regions.end();
		std::vector<region_t>::iterator found;

		wav_t w = { std::string(""), 0, 0, 0 };
		std::vector<midi_ev_t> m;
		region_t r = { std::string(""), index, 0, 0, 0, w, m};

		if ((found = std::find(begin, finish, r)) != finish) {
			ri = found;
			return true;
		}
		return false;
	}
	
	bool find_miditrack(uint16_t index, std::vector<track_t>::iterator& ti) {
		std::vector<track_t>::iterator begin = miditracks.begin();
		std::vector<track_t>::iterator finish = miditracks.end();
		std::vector<track_t>::iterator found;

		// Create dummy track with index
		wav_t w = { std::string(""), 0, 0, 0 };
		std::vector<midi_ev_t> m;
		region_t r = { std::string(""), 0, 0, 0, 0, w, m};
		track_t t = { std::string(""), index, 0, r};

		if ((found = std::find(begin, finish, t)) != finish) {
			ti = found;
			return true;
		}
		return false;
	}

	bool find_midiregion(uint16_t index, std::vector<region_t>::iterator& ri) {
		std::vector<region_t>::iterator begin = midiregions.begin();
		std::vector<region_t>::iterator finish = midiregions.end();
		std::vector<region_t>::iterator found;

		wav_t w = { std::string(""), 0, 0, 0 };
		std::vector<midi_ev_t> m;
		region_t r = { std::string(""), index, 0, 0, 0, w, m};

		if ((found = std::find(begin, finish, r)) != finish) {
			ri = found;
			return true;
		}
		return false;
	}

	bool find_wav(uint16_t index, std::vector<wav_t>::iterator& wi) {
		std::vector<wav_t>::iterator begin = audiofiles.begin();
		std::vector<wav_t>::iterator finish = audiofiles.end();
		std::vector<wav_t>::iterator found;

		wav_t w = { std::string(""), index, 0, 0 };

		if ((found = std::find(begin, finish, w)) != finish) {
			wi = found;
			return true;
		}
		return false;
	}

	static bool regionexistsin(std::vector<region_t> reg, uint16_t index) {
		std::vector<region_t>::iterator begin = reg.begin();
		std::vector<region_t>::iterator finish = reg.end();
		std::vector<region_t>::iterator found;

		wav_t w = { std::string(""), 0, 0, 0 };
		std::vector<midi_ev_t> m;
		region_t r = { std::string(""), index, 0, 0, 0, w, m};

		if ((found = std::find(begin, finish, r)) != finish) {
			return true;
		}
		return false;
	}

	static bool wavexistsin(std::vector<wav_t> wv, uint16_t index) {
		std::vector<wav_t>::iterator begin = wv.begin();
		std::vector<wav_t>::iterator finish = wv.end();
		std::vector<wav_t>::iterator found;

		wav_t w = { std::string(""), index, 0, 0 };

		if ((found = std::find(begin, finish, w)) != finish) {
			return true;
		}
		return false;
	}

	std::vector<wav_t> audiofiles;
	std::vector<region_t> regions;
	std::vector<region_t> midiregions;
	std::vector<track_t> tracks;
	std::vector<track_t> miditracks;

	std::string path;
	unsigned char *ptfunxored;
	uint64_t len;
	int64_t sessionrate;
	uint8_t version;

private:
	struct block_t;

	struct block_t {
		uint8_t zmark;			// 'Z'
		uint16_t block_type;		// type of block
		uint32_t block_size;		// size of block
		uint16_t content_type;		// type of content
		uint32_t offset;		// offset in file
		std::vector<block_t> child;	// vector of child blocks
	};
	std::vector<block_t> blocks;

	unsigned char c0;
	unsigned char c1;
	uint8_t *product;
	bool is_bigendian;
	int64_t targetrate;
	float ratefactor;

	bool jumpback(uint32_t *currpos, unsigned char *buf, const uint32_t maxoffset, const unsigned char *needle, const uint32_t needlelen);
	bool jumpto(uint32_t *currpos, unsigned char *buf, const uint32_t maxoffset, const unsigned char *needle, const uint32_t needlelen);
	bool foundin(std::string haystack, std::string needle);
	int64_t foundat(unsigned char *haystack, uint64_t n, const char *needle);
	uint16_t u_endian_read2(unsigned char *buf, bool);
	uint32_t u_endian_read3(unsigned char *buf, bool);
	uint32_t u_endian_read4(unsigned char *buf, bool);
	uint64_t u_endian_read5(unsigned char *buf, bool);
	uint64_t u_endian_read8(unsigned char *buf, bool);

	char *parsestring(uint32_t pos);
	std::string get_content_description(uint16_t ctype);
	int parse(void);
	void parseblocks(void);
	bool parseheader(void);
	bool parserest(void);
	bool parseaudio(void);
	bool parsemidi(void);
	void dump(void);
	bool parse_block_at(uint32_t pos, struct block_t *b, int level);
	void dump_block(struct block_t& b, int level);
	bool parse_version();
	void parse_region_info(uint32_t j, block_t& blk, region_t& r);
	void parse_three_point(uint32_t j, uint64_t& start, uint64_t& offset, uint64_t& length);
	uint8_t gen_xor_delta(uint8_t xor_value, uint8_t mul, bool negative);
	void setrates(void);
	void cleanup(void);
	void free_block(struct block_t& b);
	void free_all_blocks(void);
};

#endif
