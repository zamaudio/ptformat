/*
 * libptformat - a library to read ProTools sessions
 *
 * Copyright (C) 2015-2019  Damien Zammit
 * Copyright (C) 2015-2019  Robin Gareus
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
#include <math.h>
#include <time.h>
#include <map>
#include <set>
#include <string>
#include <string.h>
#include <assert.h>

#ifdef HAVE_GLIB
# include <glib/gstdio.h>
# define ptf_open	g_fopen
#else
# define ptf_open	fopen
#endif

#include "ptformat/ptformat.h"

#define BITCODE			"0010111100101011"
#define ZMARK			'\x5a'
#define ZERO_TICKS		0xe8d4a51000ULL
#define MAX_CONTENT_TYPE	0x3000

#if 0
#define DEBUG
#endif

#ifdef DEBUG
#define verbose_printf(...) printf("XXX PTFORMAT XXX: " __VA_ARGS__)
#else
#define verbose_printf(...)
#endif

using namespace std;

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

PTFFormat::PTFFormat()
	: _ptfunxored(0)
	, _len(0)
	, _sessionrate(0)
	, _sessionbitdepth(0)
	, _sessionfloat(false)
	, _fpsnum(0)
	, _fpsden(0)
	, _dropframe(false)
	, _version(0)
	, _product(NULL)
	, _targetrate (0)
	, _ratefactor (1.0)
	, is_bigendian(false)
{
}

PTFFormat::~PTFFormat() {
	cleanup();
}

const std::string
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

static uint16_t
u_endian_read2(unsigned char *buf, bool bigendian)
{
	if (bigendian) {
		return ((uint16_t)(buf[0]) << 8) | (uint16_t)(buf[1]);
	} else {
		return ((uint16_t)(buf[1]) << 8) | (uint16_t)(buf[0]);
	}
}

static uint32_t
u_endian_read3(unsigned char *buf, bool bigendian)
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

static uint32_t
u_endian_read4(unsigned char *buf, bool bigendian)
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

static uint64_t
u_endian_read5(unsigned char *buf, bool bigendian)
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

static uint64_t
u_endian_read8(unsigned char *buf, bool bigendian)
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
	_len = 0;
	_sessionrate = 0;
	_sessionbitdepth = 0;
	_sessionfloat = false;
	_version = 0;
	free(_ptfunxored);
	_ptfunxored = NULL;
	free (_product);
	_product = NULL;
	_audiofiles.clear();
	_regions.clear();
	_midiregions.clear();
	_tracks.clear();
	_miditracks.clear();
	_sessionpath.clear();
	_sourcepath.clear();
	_markers.clear();
	_plugins.clear();
	_inserts.clear();
	_volumes.clear();
	_gains.clear();
	_routes.clear();
	_strips.clear();
	_groups.clear();
	_videofiles.clear();
	_fades.clear();
	_iopaths.clear();
	_tempos.clear();
	_automation.clear();
	_fpsnum = 0;
	_fpsden = 0;
	_dropframe = false;
	free_all_blocks();
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
PTFFormat::foundin(std::string const& haystack, std::string const& needle) {
	size_t found = haystack.find(needle);
	if (found != std::string::npos) {
		return true;
	} else {
		return false;
	}
}

/* Return values:	0            success
			-1           error decrypting pt session
*/
int
PTFFormat::unxor(std::string const& path) {
	FILE *fp;
	unsigned char xxor[256];
	unsigned char ct;
	uint64_t i;
	uint8_t xor_type;
	uint8_t xor_value;
	uint8_t xor_delta;
	uint16_t xor_len;

	if (! (fp = ptf_open(path.c_str(), "rb"))) {
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	_len = ftell(fp);
	if (_len < 0x14) {
		fclose(fp);
		return -1;
	}

	if (! (_ptfunxored = (unsigned char*) malloc(_len * sizeof(unsigned char)))) {
		/* Silently fail -- out of memory*/
		fclose(fp);
		_ptfunxored = 0;
		return -1;
	}

	/* The first 20 bytes are always unencrypted */
	fseek(fp, 0x00, SEEK_SET);
	i = fread(_ptfunxored, 1, 0x14, fp);
	if (i < 0x14) {
		fclose(fp);
		return -1;
	}

	xor_type = _ptfunxored[0x12];
	xor_value = _ptfunxored[0x13];
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
		_ptfunxored[i++] = ct ^ xxor[xor_index];
	}
	fclose(fp);
	return 0;
}

int
PTFFormat::save(std::string const& path) const {
	if (!_ptfunxored || _len < 0x14 || path == _path)
		return -1;
	uint8_t xor_type = _ptfunxored[0x12];
	uint8_t xor_delta;
	switch (xor_type) {
	case 0x01:
		xor_delta = gen_xor_delta(_ptfunxored[0x13], 53, false);
		break;
	case 0x05:
		xor_delta = gen_xor_delta(_ptfunxored[0x13], 11, true);
		break;
	default:
		return -1;
	}
	std::string tmp = path + ".tmp";
	FILE *fp = ptf_open(tmp.c_str(), "wb");
	if (!fp)
		return -1;
	/* The first 20 bytes are not encrypted. */
	bool ok = fwrite(_ptfunxored, 1, 0x14, fp) == 0x14;
	unsigned char buf[4096];
	for (uint64_t i = 0x14; ok && i < _len; ) {
		size_t n = 0;
		for (; n < sizeof buf && i < _len; n++, i++) {
			uint8_t xor_index = (xor_type == 0x01) ? i & 0xff : (i >> 12) & 0xff;
			buf[n] = _ptfunxored[i] ^ (uint8_t)((xor_index * xor_delta) & 0xff);
		}
		ok = fwrite(buf, 1, n, fp) == n;
	}
	if (fclose(fp) != 0)
		ok = false;
	if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
		remove(tmp.c_str());
		return -1;
	}
	return 0;
}

bool
PTFFormat::set_muted(const track_t& placement, bool muted) {
	uint32_t e = placement.entry;
	/* The entry must be a 0x104f block of this session: its header
	 * (0x5a, type, size) sits 7 bytes before the content type. */
	if (!_ptfunxored || e < 7 || (uint64_t)e + 3 > _len
			|| _ptfunxored[e - 7] != ZMARK
			|| u_endian_read2(&_ptfunxored[e], is_bigendian) != 0x104f)
		return false;
	bool found = false;
	for (std::vector<track_t>::iterator t = _tracks.begin(); t != _tracks.end(); ++t) {
		if (t->entry == e) {
			t->reg.muted = muted;
			found = true;
		}
	}
	if (!found)
		return false;
	_ptfunxored[e + 2] = muted ? 1 : 0;
	return true;
}

static void
write4le(unsigned char *p, uint32_t v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = (v >> 24) & 0xff;
}

/* Find the slot block of a route; write value (tenths of dB for lane 0,
 * 0 or 1 for lane 1) into its 0x1029 and into the single point of that
 * lane, then update routes() and automation(). */
bool
PTFFormat::set_slot(const route_t& route, uint16_t lane, int32_t value)
{
	const block_t *slot = NULL;
	std::vector<const block_t*> todo;
	for (std::vector<block_t>::const_iterator b = blocks.begin(); b != blocks.end(); ++b)
		todo.push_back(&*b);
	while (!todo.empty() && !slot) {
		const block_t *b = todo.back();
		todo.pop_back();
		if (b->offset == route.block && b->content_type == 0x260d) {
			slot = b;
			break;
		}
		for (std::vector<block_t>::const_iterator c = b->child.begin(); c != b->child.end(); ++c)
			todo.push_back(&*c);
	}
	if (!route.block || !slot)
		return false;
	const block_t *settings = NULL, *points = NULL;
	uint16_t n = 0;
	for (std::vector<block_t>::const_iterator z = slot->child.begin(); z != slot->child.end(); ++z) {
		if (z->content_type == 0x1029 && z->block_size >= 47 && !settings)
			settings = &*z;
		if (z->content_type == 0x260a && n++ == lane)
			points = &*z;
	}
	if (!settings)
		return false;
	uint32_t pl = 0;
	if (points) {
		/* The lane layout of read_lane(): one point only. */
		pl = points->offset + 2;
		if (points->block_size != 32 || u_endian_read4(&_ptfunxored[pl], is_bigendian) != 0x00014601
				|| u_endian_read4(&_ptfunxored[pl + 10], is_bigendian) != 1)
			return false;
	}
	if (lane == 0)
		write4le(&_ptfunxored[settings->offset + 3], (uint32_t)value);
	else
		_ptfunxored[settings->offset + 7] = value ? 1 : 0;
	if (points) {
		_ptfunxored[pl + 26] = value & 0xff;
		_ptfunxored[pl + 27] = (value >> 8) & 0xff;
	}
	for (std::vector<route_t>::iterator r = _routes.begin(); r != _routes.end(); ++r) {
		if (r->block != route.block)
			continue;
		if (lane == 0)
			r->level = value / 10.0f;
		else
			r->muted = value != 0;
	}
	for (std::vector<automation_t>::iterator a = _automation.begin(); a != _automation.end(); ++a) {
		if (a->strip_index == route.strip_index && a->slot == route.slot && a->lane == lane
				&& !a->pan && a->points.size() == 1)
			a->points[0].value = lane == 0 ? value / 10.0f : value;
	}
	return true;
}

bool
PTFFormat::set_level(const route_t& route, float db)
{
	if (!(db >= -80.0f && db <= 12.0f) || is_bigendian)
		return false;
	return set_slot(route, 0, (int32_t)floor(db * 10.0f + 0.5f));
}

bool
PTFFormat::set_route_muted(const route_t& route, bool muted)
{
	if (is_bigendian)
		return false;
	return set_slot(route, 1, muted ? 1 : 0);
}

static bool
first_less(const std::pair<uint64_t, std::string>& a, const std::pair<uint64_t, std::string>& b)
{
	return a.first < b.first;
}

/* Header offsets (where the 0x5a is) of every block parsed. */
void
PTFFormat::collect_headers(const std::vector<block_t>& v, std::map<uint32_t, const block_t*>& out)
{
	for (std::vector<block_t>::const_iterator b = v.begin(); b != v.end(); ++b) {
		if (b->offset >= 0x14 + 7)
			out[b->offset - 7] = &*b;
		collect_headers(b->child, out);
	}
}

bool
PTFFormat::reparse(void) {
	unsigned char *buf = _ptfunxored;
	uint64_t len = _len;
	std::string path = _path;
	int64_t targetrate = _targetrate;
	_ptfunxored = NULL;
	cleanup();
	_ptfunxored = buf;
	_len = len;
	_path = path;
	if (parse_version() || _version < 5 || _version > 12)
		return false;
	_targetrate = targetrate;
	return parse() == 0;
}

/* The 0x2629 of a region under the top-level 0x262a, counted the way
 * parserest() numbers regions, its first child (the 0x2628, which must
 * start right after the content type) and the list holding them. */
bool
PTFFormat::region_blocks(uint16_t index, const block_t*& list, const block_t*& def, const block_t*& info) const {
	uint16_t n = 0;
	for (std::vector<block_t>::const_iterator b = blocks.begin(); b != blocks.end(); ++b) {
		if (b->content_type == 0x100b)
			return false;
		if (b->content_type != 0x262a)
			continue;
		for (std::vector<block_t>::const_iterator c = b->child.begin(); c != b->child.end(); ++c) {
			if (c->content_type != 0x2629 || c->child.empty())
				continue;
			if (n++ == index) {
				list = &*b;
				def = &*c;
				info = &c->child[0];
				return info->offset == def->offset + 9;
			}
		}
	}
	return false;
}

/* Where a region's 0x2628 (info) keeps the index of its clip gain point
 * (see parserest); 0 when the block is too short. */
uint32_t
PTFFormat::gain_field(const block_t& info) const {
	uint32_t tail = info.offset + info.block_size;
	if (info.block_size >= 16 && tail + 39 <= _len && _ptfunxored[tail + 22] == 0xff &&
			memcmp(&_ptfunxored[tail - 16], &_ptfunxored[tail + 23], 16) == 0)
		tail -= 16;
	if (tail - info.offset < 19)
		return 0;
	return tail - 6;
}

/* Replace the oldlen bytes at at with data: the blocks in grow (those
 * holding at) change size, every later block moves, and the offsets
 * pointing at moved blocks are moved too: the pointer of block 0x0001
 * to the final 0x0002 block, the target of each standard 15-byte record
 * of that block (pt_api format notes, section 2.2), and each start of a
 * parsed block stored after the content type of one of that block's
 * children (2 bytes), the form the other pointers of the 0x0002 block
 * take in the sessions seen. The pt_api notes move any 4 bytes that hold
 * the start of a block; in the sessions seen that also moves values that
 * only look like one, inside lists of small offsets or runs of ff, so it
 * is not done here. Two such places that overlap make the edit
 * ambiguous, and it is refused. Each pair in set writes a 4-byte value
 * at an offset of the current buffer, outside the bytes replaced. Then
 * the session is parsed again. False, with nothing changed, when the
 * session does not have the expected layout. */
bool
PTFFormat::splice(uint32_t at, uint32_t oldlen, const std::string& data,
		const std::vector<const block_t*>& grow,
		const std::vector<std::pair<uint32_t, uint32_t> >& set) {
	if (!_ptfunxored || is_bigendian || _ptfunxored[0x12] != 0x05 || (uint64_t)at + oldlen > _len)
		return false;
	uint32_t from = at + oldlen;	/* first byte that moves */
	for (std::vector<std::pair<uint32_t, uint32_t> >::const_iterator w = set.begin(); w != set.end(); ++w) {
		if ((uint64_t)w->first + 4 > _len || (w->first + 4 > at && w->first < from))
			return false;
	}

	/* The pointer at 0x14 (block 0x0001) must name the last block, a
	 * 0x0002 that ends the file. */
	uint32_t last = u_endian_read4(&_ptfunxored[0x14 + 7], false);
	if (_ptfunxored[0x14] != ZMARK || u_endian_read2(&_ptfunxored[0x15], false) != 0x0001
			|| (uint64_t)last + 7 > _len || last < from || _ptfunxored[last] != ZMARK
			|| u_endian_read2(&_ptfunxored[last + 1], false) != 0x0002
			|| (uint64_t)last + 7 + u_endian_read4(&_ptfunxored[last + 3], false) != _len)
		return false;

	int64_t delta = (int64_t)data.size() - (int64_t)oldlen;
	std::map<uint32_t, const block_t*> headers;
	collect_headers(blocks, headers);

	static const unsigned char rec[8] = { 0, 0, 0, 1, 4, 0, 1, 0 };
	std::vector<uint32_t> patch;
	std::vector<bool> inrec(_len - last, false);
	for (uint64_t i = last + 7; i + 15 <= _len; ) {
		if (memcmp(&_ptfunxored[i], rec, 8) == 0 && !_ptfunxored[i + 12]
				&& !_ptfunxored[i + 13] && !_ptfunxored[i + 14]) {
			uint32_t t = u_endian_read4(&_ptfunxored[i + 8], false);
			if (!headers.count(t))
				return false;
			if (t >= from)
				patch.push_back(i + 8);
			for (int k = 0; k < 15; k++)
				inrec[i - last + k] = true;
			i += 15;
		} else {
			i++;
		}
	}
	uint32_t prev = 0;
	for (uint64_t i = last + 7; i + 4 <= _len; i++) {
		if (inrec[i - last] || inrec[i - last + 3])
			continue;
		uint32_t t = u_endian_read4(&_ptfunxored[i], false);
		if (t < from || i < last + 9)
			continue;
		std::map<uint32_t, const block_t*>::const_iterator h = headers.find(t);
		if (h == headers.end())
			continue;
		uint16_t kind = u_endian_read2(&_ptfunxored[i - 2], false);
		bool child = false;
		for (std::vector<block_t>::const_iterator k = h->second->child.begin();
				k != h->second->child.end() && !child; ++k)
			child = (k->content_type == kind);
		if (!child)
			continue;
		if (prev && i < prev + 4)
			return false;
		patch.push_back(i);
		prev = i;
	}

	/* Build the new buffer. */
	uint64_t newlen = _len + delta;
	unsigned char *buf = (unsigned char *)malloc(newlen);
	if (!buf)
		return false;
	memcpy(buf, _ptfunxored, at);
	memcpy(&buf[at], data.data(), data.size());
	memcpy(&buf[at + data.size()], &_ptfunxored[from], _len - from);

	for (std::vector<const block_t*>::const_iterator g = grow.begin(); g != grow.end(); ++g) {
		uint32_t size = (*g)->block_size + delta;
		write4le(&buf[(*g)->offset - 7 + 3], size);
	}
	for (std::vector<uint32_t>::const_iterator p = patch.begin(); p != patch.end(); ++p) {
		uint32_t t = u_endian_read4(&_ptfunxored[*p], false) + delta;
		write4le(&buf[*p + delta], t);
	}
	for (std::vector<std::pair<uint32_t, uint32_t> >::const_iterator w = set.begin(); w != set.end(); ++w)
		write4le(&buf[w->first < at ? w->first : w->first + delta], w->second);
	uint32_t newlast = last + delta;
	write4le(&buf[0x14 + 7], newlast);

	free(_ptfunxored);
	_ptfunxored = buf;
	_len = newlen;
	return reparse();
}

bool
PTFFormat::rename_region(const region_t& region, const std::string& name) {
	if (!_ptfunxored || name.empty() || name.size() > 0xffff || name.find('\0') != std::string::npos)
		return false;
	for (std::vector<region_t>::const_iterator r = _regions.begin(); r != _regions.end(); ++r) {
		if (r->name == name)
			return false;
	}
	/* The 0x2628 holds the name length and the name first. */
	const block_t *list, *def, *info;
	if (!region_blocks(region.index, list, def, info))
		return false;
	uint32_t at = info->offset + 2;
	uint32_t oldlen = u_endian_read4(&_ptfunxored[at], false);
	if (oldlen > info->block_size - 6 || oldlen != region.name.size()
			|| memcmp(&_ptfunxored[at + 4], region.name.data(), oldlen) != 0)
		return false;

	unsigned char len[4];
	write4le(len, name.size());
	std::string data((const char *)len, 4);
	data += name;
	std::vector<const block_t*> grow;
	grow.push_back(info);
	grow.push_back(def);
	grow.push_back(list);
	return splice(at, 4 + oldlen, data, grow, std::vector<std::pair<uint32_t, uint32_t> >());
}

/* The playlist (0x1052) holding the 0x104f entry at e, its 0x1050
 * entries in order (stored back to back, each with one 0x104f) with the
 * time each covers, and which of them is e. */
bool
PTFFormat::playlist_of(uint32_t e, const block_t*& list, std::vector<playlist_slot>& slots, size_t& self) {
	if (!_ptfunxored || is_bigendian || _ratefactor <= 0 || e < 7
			|| (uint64_t)e + 17 > _len || _ptfunxored[e - 7] != ZMARK
			|| u_endian_read2(&_ptfunxored[e], false) != 0x104f)
		return false;

	list = NULL;
	slots.clear();
	for (std::vector<block_t>::const_iterator b = blocks.begin(); b != blocks.end() && !list; ++b) {
		if (b->content_type != 0x1054)
			continue;
		for (std::vector<block_t>::const_iterator l = b->child.begin(); l != b->child.end() && !list; ++l) {
			if (l->content_type != 0x1052)
				continue;
			for (std::vector<block_t>::const_iterator c = l->child.begin(); c != l->child.end(); ++c) {
				if (c->content_type == 0x1050 && c->child.size() == 1 && c->child[0].offset == e) {
					list = &*l;
					break;
				}
			}
		}
	}
	if (!list)
		return false;

	for (std::vector<block_t>::const_iterator c = list->child.begin(); c != list->child.end(); ++c) {
		if (c->content_type != 0x1050)
			continue;
		if (c->child.size() != 1 || c->child[0].content_type != 0x104f || c->child[0].block_size < 17
				|| c->block_size < 47)
			return false;
		playlist_slot sl;
		sl.start = c->offset - 7;
		sl.end = c->offset + c->block_size;
		if (!slots.empty() && sl.start != slots.back().end)
			return false;
		sl.entry = c->child[0].offset;
		sl.fade = (_ptfunxored[c->offset + 46] == 0x01);
		uint64_t stored = u_endian_read8(&_ptfunxored[sl.entry + 9], false);
		/* A damaged file can hold any value: keep to positions that
		 * convert safely. */
		if (stored > ((uint64_t)1 << 48))
			return false;
		int64_t at = (int64_t)(stored * _ratefactor);
		int64_t length = 0;
		if (sl.fade) {
			fade_t fd;
			if (!parsefade(u_endian_read4(&_ptfunxored[sl.entry + 4], false), fd))
				return false;
			/* Whether the fade starts or ends at the stored position,
			 * cover both. */
			sl.from = at - fd.length;
			sl.to = at + fd.length;
		} else {
			region_t r;
			if (!find_region(u_endian_read4(&_ptfunxored[sl.entry + 4], false), r))
				return false;
			length = r.length;
			sl.from = at;
			sl.to = at + length;
		}
		slots.push_back(sl);
	}

	self = slots.size();
	for (size_t i = 0; i < slots.size(); i++) {
		if (slots[i].entry == e)
			self = i;
	}
	return self < slots.size() && !slots[self].fade;
}

bool
PTFFormat::set_position(const track_t& placement, int64_t pos) {
	uint32_t e = placement.entry;
	const block_t *list;
	std::vector<playlist_slot> slots;
	size_t self;
	if (pos < 0 || !playlist_of(e, list, slots, self))
		return false;
	int64_t length = slots[self].to - slots[self].from;
	int64_t lo = std::min(slots[self].from, pos), hi = std::max(slots[self].to, pos + length);
	for (size_t i = 0; i < slots.size(); i++) {
		if (i == self)
			continue;
		/* No fade on, next to or between the old and new places, and
		 * no overlap with another region of the playlist. */
		if (slots[i].fade && slots[i].from <= hi && slots[i].to >= lo)
			return false;
		if (!slots[i].fade && slots[i].from < pos + length && slots[i].to > pos)
			return false;
	}

	uint64_t raw = (uint64_t)((double)pos / _ratefactor + 0.5);
	for (int k = 0; k < 8; k++)
		_ptfunxored[e + 9 + k] = (raw >> (8 * k)) & 0xff;

	/* Keep the audio entries in time order: put them back, sorted, in the
	 * places audio entries take; fade entries stay where they are. */
	std::vector<std::pair<uint64_t, std::string> > audio;
	for (size_t i = 0; i < slots.size(); i++) {
		if (slots[i].fade)
			continue;
		uint64_t t = u_endian_read8(&_ptfunxored[slots[i].entry + 9], false);
		audio.push_back(std::make_pair(t, std::string((const char *)&_ptfunxored[slots[i].start],
			slots[i].end - slots[i].start)));
	}
	std::stable_sort(audio.begin(), audio.end(), first_less);
	std::string bytes;
	size_t next = 0;
	for (size_t i = 0; i < slots.size(); i++) {
		if (slots[i].fade)
			bytes.append((const char *)&_ptfunxored[slots[i].start], slots[i].end - slots[i].start);
		else
			bytes += audio[next++].second;
	}
	memcpy(&_ptfunxored[slots[0].start], bytes.data(), bytes.size());
	return reparse();
}

/* Little-endian value of n bytes at p. */
static uint64_t
readle(const unsigned char *p, int n)
{
	uint64_t v = 0;
	for (int i = 0; i < n; i++)
		v |= (uint64_t)p[i] << (8 * i);
	return v;
}

static void
appendle(std::string& s, uint64_t v, int n)
{
	for (int i = 0; i < n; i++)
		s += (char)((v >> (8 * i)) & 0xff);
}

/* Bytes needed for v, 0 for 0. */
static int
width(uint64_t v)
{
	int w = 0;
	while (v) {
		w++;
		v >>= 8;
	}
	return w;
}

/* 16 random bytes shaped as a version 4 UUID. */
static std::string
new_uuid(void)
{
	unsigned char b[16];
	FILE *fp = fopen("/dev/urandom", "rb");
	bool ok = fp && fread(b, 1, 16, fp) == 16;
	if (fp)
		fclose(fp);
	if (!ok) {
		srand((unsigned)time(NULL) ^ (unsigned)clock());
		for (int i = 0; i < 16; i++)
			b[i] = rand() & 0xff;
	}
	b[6] = (b[6] & 0x0f) | 0x40;
	b[8] = (b[8] & 0x3f) | 0x80;
	return std::string((const char *)b, 16);
}

/* Whether a 0x2a marker and the 8-byte id (16 hex digits) start within
 * 12 bytes from pos, before end. */
static bool
holds_id(const unsigned char *buf, uint32_t pos, uint32_t end, const std::string& id)
{
	static const char hexdigits[] = "0123456789abcdef";
	for (uint32_t i = pos; i < pos + 12 && i + 12 <= end; i++) {
		if (readle(&buf[i], 4) != 0x2a)
			continue;
		std::string got;
		for (uint32_t k = 4; k < 12; k++) {
			got += hexdigits[buf[i + k] >> 4];
			got += hexdigits[buf[i + k] & 0x0f];
		}
		return got == id;
	}
	return false;
}

/* Every place that stores the name of the track with id (see
 * rename_track()), with the blocks that hold it, innermost first. False
 * when a list does not have the expected layout. */
bool
PTFFormat::track_name_fields(const std::string& id, const std::string& name, std::vector<name_field>& out) const
{
	out.clear();
	const unsigned char *d = _ptfunxored;
	/* A length-prefixed name at at, inside [.., end): true when it is
	 * name. */
	struct local {
		static bool named(const unsigned char *d, uint32_t at, uint32_t end, const std::string& name) {
			if (at > end || end - at < 4)
				return false;
			uint64_t len = readle(&d[at], 4);
			return len == name.size() && len <= end - at - 4 && !memcmp(&d[at + 4], name.data(), len);
		}
	};
	for (std::vector<block_t>::const_iterator b = blocks.begin(); b != blocks.end(); ++b) {
		uint32_t bend = b->offset + b->block_size;
		if (b->content_type == 0x2519) {
			/* The record list before the first 0x251a: a count at +18,
			 * then per track its type (2 bytes), name, format (2),
			 * 4 zero bytes, the 0x2a marker and id, and 3 bytes. */
			uint32_t lend = b->child.empty() ? bend : b->child[0].offset - 7;
			if (lend < b->offset + 22)
				return false;
			uint32_t n = readle(&d[b->offset + 18], 4), pos = b->offset + 22;
			for (uint32_t i = 0; i < n; i++) {
				if (pos + 6 > lend)
					return false;
				uint32_t len = readle(&d[pos + 2], 4);
				if (len > lend - pos - 6 || lend - pos - 6 - len < 21
						|| readle(&d[pos + 6 + len + 6], 4) != 0x2a)
					return false;
				/* A place of the track that no longer holds the name
				 * (renamed already) is left out; rename_track() checks
				 * the count. */
				if (holds_id(d, pos + 6 + len + 6, lend, id) && local::named(d, pos + 2, lend, name)) {
					name_field f;
					f.at = pos + 2;
					f.grow.push_back(&*b);
					out.push_back(f);
				}
				pos += 6 + len + 21;
			}
			for (std::vector<block_t>::const_iterator c = b->child.begin(); c != b->child.end(); ++c) {
				uint32_t cend = c->offset + c->block_size;
				if (c->content_type != 0x251a || c->block_size < 8)
					continue;
				uint32_t len = readle(&d[c->offset + 4], 4);
				if (len > c->block_size - 8 || !holds_id(d, c->offset + 8 + len, cend, id)
						|| !local::named(d, c->offset + 4, cend, name))
					continue;
				name_field f;
				f.at = c->offset + 4;
				f.grow.push_back(&*c);
				f.grow.push_back(&*b);
				out.push_back(f);
			}
		}
		if (b->content_type == 0x2107 || b->content_type == 0x1015) {
			uint16_t kind = b->content_type == 0x2107 ? 0x210b : 0x1014;
			uint32_t lenat = kind == 0x210b ? 6 : 2;
			for (std::vector<block_t>::const_iterator c = b->child.begin(); c != b->child.end(); ++c) {
				uint32_t cend = c->offset + c->block_size;
				if (c->content_type != kind || c->block_size < lenat + 4)
					continue;
				uint32_t len = readle(&d[c->offset + lenat], 4);
				if (len > c->block_size - lenat - 4)
					continue;
				uint32_t after = c->offset + lenat + 4 + len;
				/* The id a 0x1014 holds is not the track's (it differs
				 * in one byte in the sessions seen): match it by name,
				 * as track names are unique. */
				if (!local::named(d, c->offset + lenat, cend, name)
						|| (kind == 0x210b && !holds_id(d, after, cend, id)))
					continue;
				name_field f;
				f.at = c->offset + lenat;
				f.grow.push_back(&*c);
				f.grow.push_back(&*b);
				out.push_back(f);
			}
		}
		/* An audio track's strip holds its playlist name, which may
		 * equal the track name: leave it, with the playlist. */
		bool audio = false;
		for (std::vector<strip_t>::const_iterator t = _strips.begin(); t != _strips.end(); ++t) {
			if (t->id == id && t->type == 0)
				audio = true;
		}
		if (b->content_type == 0x2624 && !audio) {
			for (std::vector<block_t>::const_iterator s = b->child.begin(); s != b->child.end(); ++s)
			for (std::vector<block_t>::const_iterator x = s->child.begin(); x != s->child.end(); ++x) {
				if (x->content_type != 0x261b)
					continue;
				for (std::vector<block_t>::const_iterator y = x->child.begin(); y != x->child.end(); ++y) {
					if (y->content_type != 0x102d)
						continue;
					for (std::vector<block_t>::const_iterator z = y->child.begin(); z != y->child.end(); ++z) {
						uint32_t zend = z->offset + z->block_size;
						if (z->content_type != 0x2619 || z->block_size < 6)
							continue;
						uint32_t len = readle(&d[z->offset + 2], 4);
						if (len > z->block_size - 6 || !holds_id(d, z->offset + 6 + len, zend, id))
							continue;
						if (!local::named(d, z->offset + 2, zend, name))
							continue;
						name_field f;
						f.at = z->offset + 2;
						f.grow.push_back(&*z);
						f.grow.push_back(&*y);
						f.grow.push_back(&*x);
						f.grow.push_back(&*s);
						f.grow.push_back(&*b);
						out.push_back(f);
					}
				}
			}
		}
	}
	return true;
}

bool
PTFFormat::rename_track(const std::string& id, const std::string& name) {
	if (!_ptfunxored || is_bigendian || name.empty() || name.size() > 0xffff
			|| name.find('\0') != std::string::npos)
		return false;
	std::string old;
	bool found = false;
	for (std::vector<strip_t>::const_iterator t = _strips.begin(); t != _strips.end(); ++t) {
		if (t->name == name)
			return false;
		if (t->id == id) {
			old = t->name;
			found = true;
		}
	}
	if (!found || old.empty())
		return false;
	std::vector<name_field> fields;
	if (!track_name_fields(id, old, fields) || fields.size() < 4)
		return false;
	size_t total = fields.size();

	/* Keep the session as it is, to restore it if a step fails. */
	std::string saved((const char *)_ptfunxored, _len);
	std::string data;
	appendle(data, name.size(), 4);
	data += name;
	bool ok = true;
	/* Each splice parses the session again: find the places anew and
	 * rename the first one left with the old name. */
	for (size_t done = 0; done < total && ok; done++) {
		ok = track_name_fields(id, old, fields) && fields.size() == total - done
			&& splice(fields[0].at, 4 + old.size(), data, fields[0].grow,
				std::vector<std::pair<uint32_t, uint32_t> >());
	}
	if (ok && (!track_name_fields(id, old, fields) || !fields.empty()))
		ok = false;
	if (!ok) {
		unsigned char *buf = (unsigned char *)malloc(saved.size());
		if (buf) {
			memcpy(buf, saved.data(), saved.size());
			free(_ptfunxored);
			_ptfunxored = buf;
			_len = saved.size();
		}
		reparse();
	}
	return ok;
}

/* A memory location (0x2077): its number at +2, the bytes 03 01 00 00 for
 * a marker, its name at +8 and its start and end (8 bytes each); then
 * per-track 0x2506 blocks and, 23 bytes after the last of them, its
 * 16-byte UUID (version 4, unique, referenced nowhere else in all the
 * sessions seen), before the first 0x4826. The 0x2030 list holds a count
 * at +2 and the locations in number order; number 10000 is a location
 * Pro Tools keeps last. */
bool
PTFFormat::add_marker(const std::string& name, int64_t pos) {
	if (!_ptfunxored || is_bigendian || _ratefactor != 1.0 || name.empty()
			|| name.size() > 0xffff || name.find('\0') != std::string::npos
			|| pos < 0 || pos > ((int64_t)1 << 48))
		return false;
	const block_t *list = NULL;
	for (std::vector<block_t>::const_iterator b = blocks.begin(); b != blocks.end() && !list; ++b) {
		if (b->content_type == 0x2030 && b->block_size >= 6)
			list = &*b;
	}
	if (!list)
		return false;
	uint32_t count = u_endian_read4(&_ptfunxored[list->offset + 2], false);
	const block_t *tmpl = NULL, *after = NULL;
	uint32_t top = 0, n = 0;
	for (std::vector<block_t>::const_iterator m = list->child.begin(); m != list->child.end(); ++m) {
		if (m->content_type != 0x2077 || m->block_size < 12)
			return false;
		n++;
		const unsigned char *d = &_ptfunxored[m->offset];
		uint16_t idx = (uint16_t)readle(&d[2], 2);
		uint32_t len = (uint32_t)readle(&d[8], 4);
		if (len > m->block_size - 12 || m->block_size - 12 - len < 16)
			return false;
		bool marker = d[4] == 3 && d[5] == 1 && !d[6] && !d[7]
			&& !memcmp(&d[12 + len], &d[20 + len], 8);
		if (idx < 10000) {
			if (idx > top)
				top = idx;
			after = &*m;
			if (marker && !tmpl)
				tmpl = &*m;
		}
	}
	if (!tmpl || !after || n != count || top + 1 >= 10000)
		return false;

	/* The template's UUID: 23 bytes after the block before its first
	 * 0x4826. */
	uint32_t prevend = 0;
	bool found = false;
	for (std::vector<block_t>::const_iterator k = tmpl->child.begin(); k != tmpl->child.end(); ++k) {
		if (k->content_type == 0x4826) {
			found = prevend && k->offset - 7 >= prevend + 39;
			break;
		}
		prevend = k->offset + k->block_size;
	}
	if (!found)
		return false;
	uint32_t head = tmpl->offset - 7;
	uint32_t oldlen = u_endian_read4(&_ptfunxored[tmpl->offset + 8], false);
	std::string blk((const char *)&_ptfunxored[head], tmpl->block_size + 7);
	uint32_t uid = prevend + 23 - head;
	uint32_t pstart = 7 + 12 + oldlen;	/* start, in blk */
	std::string out = blk.substr(0, 7 + 8);
	appendle(out, name.size(), 4);
	out += name;
	appendle(out, (uint64_t)pos, 8);
	appendle(out, (uint64_t)pos, 8);
	out += blk.substr(pstart + 16, uid - (pstart + 16));
	out += new_uuid();
	out += blk.substr(uid + 16);
	/* Number, then the block size. */
	out[7 + 2] = (top + 1) & 0xff;
	out[7 + 3] = ((top + 1) >> 8) & 0xff;
	uint32_t size = out.size() - 7;
	out[3] = size & 0xff;
	out[4] = (size >> 8) & 0xff;
	out[5] = (size >> 16) & 0xff;
	out[6] = (size >> 24) & 0xff;

	std::vector<const block_t*> grow(1, list);
	std::vector<std::pair<uint32_t, uint32_t> > set(1, std::make_pair(list->offset + 2, count + 1));
	return splice(after->offset + after->block_size, 0, out, grow, set);
}

/* A new 0x2629 for a part of the region whose 0x2629 is def (with info,
 * its 0x2628): the same bytes with another name, the part's offset and
 * length in the file, a new UUID and the 8-byte id id. Sub-regions
 * Pro Tools makes store 01 <offset width << 4> <length width << 4> 44 08,
 * the offset and length in the fewest bytes, the region's two file
 * references moved by the offset, eight ff bytes and then the rest of
 * the whole-file layout, with the recording track cleared. Empty when
 * the region has another layout. */
std::string
PTFFormat::region_part(const block_t& def, const block_t& info, const std::string& name,
		uint64_t offset, uint64_t length, const std::string& id) const {
	uint32_t at = info.offset + 2;
	uint32_t nl = u_endian_read4(&_ptfunxored[at], false);
	uint32_t end = info.offset + info.block_size;
	uint32_t A = at + 4 + nl;
	if (nl > info.block_size || A + 5 > end || _ptfunxored[A + 3] != 0x44)
		return "";
	int wo = _ptfunxored[A + 1] >> 4, wl = _ptfunxored[A + 2] >> 4;
	if ((_ptfunxored[A + 1] & 0x0f) || (_ptfunxored[A + 2] & 0x0f) || wo > 4 || wl < 1 || wl > 4
			|| _ptfunxored[A] > 1)
		return "";
	bool whole = (_ptfunxored[A + 4] == 0x00);
	if (whole ? (wo != 0 || _ptfunxored[A] != 0) : _ptfunxored[A + 4] != 0x08)
		return "";
	uint32_t p = A + 5 + wo + wl;	/* the two file references */
	uint32_t tail = p + 8 + (whole ? 0 : 8);
	if (tail + 20 > end)
		return "";
	if (!whole && memcmp(&_ptfunxored[p + 8], "\xff\xff\xff\xff\xff\xff\xff\xff", 8) != 0)
		return "";
	uint64_t oldoff = readle(&_ptfunxored[A + 5], wo);
	if (length == 0 || offset < oldoff || width(offset) > 4 || width(length) > 4)
		return "";
	uint32_t move = (uint32_t)(offset - oldoff);

	std::string mid;
	mid += (char)0x01;
	mid += (char)(width(offset) << 4);
	mid += (char)(width(length) << 4);
	mid += (char)0x44;
	mid += (char)0x08;
	appendle(mid, offset, width(offset));
	appendle(mid, length, width(length));
	appendle(mid, (uint32_t)(u_endian_read4(&_ptfunxored[p], false) + move), 4);
	appendle(mid, (uint32_t)(u_endian_read4(&_ptfunxored[p + 4], false) + move), 4);
	mid.append(8, '\xff');
	std::string rest((const char *)&_ptfunxored[tail], end - tail);
	if (whole && rest.size() >= 33 && memcmp(rest.data() + 20, "\x2a\0\0\0", 4) == 0)
		rest.replace(20, 13, std::string(13, '\0'));
	mid += rest;

	std::string info_payload((const char *)&_ptfunxored[info.offset], 2);
	appendle(info_payload, name.size(), 4);
	info_payload += name + mid;
	std::string after((const char *)&_ptfunxored[end], def.offset + def.block_size - end);

	std::string out((const char *)&_ptfunxored[def.offset - 7], 3);
	appendle(out, 2 + 7 + info_payload.size() + after.size(), 4);
	out.append((const char *)&_ptfunxored[def.offset], 2);
	out.append((const char *)&_ptfunxored[info.offset - 7], 3);
	appendle(out, info_payload.size(), 4);
	out += info_payload + after;

	/* The UUID is stored after a 0xff in the rest of the 0x2629 and,
	 * in newer saves, at the end of the 0x2628; replace every copy. */
	uint32_t u = end + 23;
	if (u + 16 > def.offset + def.block_size || _ptfunxored[end + 22] != 0xff)
		return "";
	std::string olduid((const char *)&_ptfunxored[u], 16), uid = new_uuid();
	for (size_t i = out.find(olduid); i != std::string::npos; i = out.find(olduid, i + 16))
		out.replace(i, 16, uid);
	/* The 8-byte id after the 0x2a 21 bytes from the end. */
	if (out.size() < 21 || readle((const unsigned char *)out.data() + out.size() - 21, 4) != 0x2a)
		return "";
	out.replace(out.size() - 17, 8, id);
	return out;
}

bool
PTFFormat::split_region(const track_t& placement, int64_t cut) {
	if (_ratefactor != 1.0)
		return false;
	const block_t *list;
	std::vector<playlist_slot> slots;
	size_t self;
	uint32_t e = placement.entry;
	if (!playlist_of(e, list, slots, self))
		return false;
	int64_t start = slots[self].from, end = slots[self].to;
	if (cut <= start || cut >= end)
		return false;
	for (size_t i = 0; i < slots.size(); i++) {
		if (slots[i].fade && slots[i].from <= end && slots[i].to >= start)
			return false;
	}
	region_t r;
	if (!find_region(u_endian_read4(&_ptfunxored[e + 4], false), r) || (int64_t)r.length != end - start)
		return false;
	const block_t *regions, *def, *info;
	if (!region_blocks(r.index, regions, def, info))
		return false;
	uint32_t count = u_endian_read4(&_ptfunxored[regions->offset + 2], false);
	uint32_t n = 0;
	for (std::vector<block_t>::const_iterator c = regions->child.begin(); c != regions->child.end(); ++c)
		n += (c->content_type == 0x2629);
	if (count != n || n != _regions.size())
		return false;

	/* Names as Pro Tools gives them: the region's name, a dash and the
	 * next two numbers after the highest already used. */
	std::string base = r.name;
	int high = 0;
	for (std::vector<region_t>::const_iterator o = _regions.begin(); o != _regions.end(); ++o) {
		if (o->name.size() > base.size() + 1 && o->name.compare(0, base.size() + 1, base + "-") == 0) {
			const char *d = o->name.c_str() + base.size() + 1;
			char *stop;
			long v = strtol(d, &stop, 10);
			if (!*stop && v > high)
				high = (int)v;
		}
	}
	char left[16], right[16];
	snprintf(left, sizeof left, "-%02d", high + 1);
	snprintf(right, sizeof right, "-%02d", high + 2);

	/* Two new 8-byte ids after the highest in use, found nowhere else. */
	uint64_t top = 0;
	for (std::vector<block_t>::const_iterator c = regions->child.begin(); c != regions->child.end(); ++c) {
		uint32_t z = c->offset + c->block_size;
		if (c->content_type == 0x2629 && c->block_size >= 21
				&& u_endian_read4(&_ptfunxored[z - 21], false) == 0x2a) {
			uint64_t v = readle(&_ptfunxored[z - 17], 8);
			if (v > top)
				top = v;
		}
	}
	std::string ids[2];
	for (int k = 0; k < 2; k++) {
		appendle(ids[k], top + 1 + k, 8);
		for (uint64_t i = 0; i + 8 <= _len; i++) {
			if (memcmp(&_ptfunxored[i], ids[k].data(), 8) == 0)
				return false;
		}
	}

	uint64_t into = (uint64_t)(cut - start);
	std::string a = region_part(*def, *info, base + left, r.sampleoffset, into, ids[0]);
	std::string b = region_part(*def, *info, base + right, r.sampleoffset + into, r.length - into, ids[1]);
	if (a.empty() || b.empty())
		return false;

	/* The playlist entry count follows the playlist name. */
	uint32_t pl = list->offset + 2;
	uint32_t namelen = u_endian_read4(&_ptfunxored[pl], false);
	uint32_t countat = pl + 4 + namelen;
	if (namelen > list->block_size || countat + 4 > list->offset + list->block_size
			|| u_endian_read4(&_ptfunxored[countat], false) != slots.size())
		return false;

	/* Keep the session as it is, to restore it if the second step fails. */
	std::string saved((const char *)_ptfunxored, _len);

	/* 1. Add both regions at the end of the region list. */
	uint32_t at = regions->offset + regions->block_size;
	std::vector<const block_t*> grow(1, regions);
	std::vector<std::pair<uint32_t, uint32_t> > set;
	set.push_back(std::make_pair(regions->offset + 2, count + 2));
	int64_t added = (int64_t)(a.size() + b.size());
	if (!splice(at, 0, a + b, grow, set))
		return false;
	if (e >= at)
		e += added;

	/* 2. The placement plays the left part; a copy of its entry right
	 * after it plays the right part from the cut. */
	bool ok = playlist_of(e, list, slots, self);
	if (ok) {
		pl = list->offset + 2;
		countat = pl + 4 + u_endian_read4(&_ptfunxored[pl], false);
		std::string entry((const char *)&_ptfunxored[slots[self].start], slots[self].end - slots[self].start);
		uint32_t idx = e - slots[self].start + 4;
		uint32_t stamp = e - slots[self].start + 9;
		std::string copy = entry;
		for (int k = 0; k < 4; k++)
			copy[idx + k] = (char)(((n + 1) >> (8 * k)) & 0xff);
		for (int k = 0; k < 8; k++)
			copy[stamp + k] = (char)(((uint64_t)cut >> (8 * k)) & 0xff);
		const block_t *map = NULL;
		for (std::vector<block_t>::const_iterator m = blocks.begin(); m != blocks.end() && !map; ++m) {
			if (m->content_type != 0x1054)
				continue;
			for (std::vector<block_t>::const_iterator l = m->child.begin(); l != m->child.end(); ++l) {
				if (&*l == list)
					map = &*m;
			}
		}
		ok = map != NULL;
		if (ok) {
			std::vector<const block_t*> grow2;
			grow2.push_back(list);
			grow2.push_back(map);
			std::vector<std::pair<uint32_t, uint32_t> > set2;
			set2.push_back(std::make_pair(e + 4, n));
			set2.push_back(std::make_pair(countat, (uint32_t)slots.size() + 1));
			ok = splice(slots[self].end, 0, copy, grow2, set2);
		}
	}
	if (!ok) {
		unsigned char *buf = (unsigned char *)malloc(saved.size());
		if (buf) {
			memcpy(buf, saved.data(), saved.size());
			free(_ptfunxored);
			_ptfunxored = buf;
			_len = saved.size();
		}
		reparse();
		return false;
	}
	return true;
}

bool
PTFFormat::add_fade(const track_t& placement, bool fade_in, int64_t length, uint8_t shape) {
	if (_ratefactor != 1.0 || length <= 0 || width(length) > 4 || (shape != 1 && shape != 2))
		return false;
	const block_t *list;
	std::vector<playlist_slot> slots;
	size_t self;
	uint32_t e = placement.entry;
	if (!playlist_of(e, list, slots, self))
		return false;
	int64_t start = slots[self].from, end = slots[self].to;
	if (length > end - start)
		return false;
	int64_t edge = fade_in ? start : end;
	for (size_t i = 0; i < slots.size(); i++) {
		if (i == self)
			continue;
		/* No fade on that edge already, and no region touching it (that
		 * would call for a crossfade). */
		if (slots[i].fade && slots[i].from <= edge + length && slots[i].to >= edge - length)
			return false;
		if (!slots[i].fade && slots[i].from <= end && slots[i].to >= start)
			return false;
	}

	/* The fade list: the single top-level 0x2630, a count and the 0x262f
	 * records; a new record copies the header of an existing one. */
	const block_t *fades = NULL, *model = NULL;
	for (std::vector<block_t>::const_iterator b = blocks.begin(); b != blocks.end(); ++b) {
		if (b->content_type != 0x2630)
			continue;
		if (fades)
			return false;
		fades = &*b;
	}
	if (!fades || fades->block_size < 6)
		return false;
	uint32_t count = u_endian_read4(&_ptfunxored[fades->offset + 2], false), n = 0;
	for (std::vector<block_t>::const_iterator c = fades->child.begin(); c != fades->child.end(); ++c) {
		if (c->content_type == 0x262f) {
			n++;
			model = &*c;
		}
	}
	if (!model || n != count)
		return false;

	/* The record as Pro Tools writes them in the sessions seen: 5 zero
	 * bytes, the widths of two values (high and low nibble), 2 zero
	 * bytes, the values, the type (3 fade-in, 2 fade-out) and the shape,
	 * then 10 zero bytes for a fade-in or 13 for a fade-out. A fade-in
	 * holds its length once, a fade-out twice. */
	int w = width(length);
	std::string geo(5, '\0');
	geo += (char)(fade_in ? (w << 4) : ((w << 4) | w));
	geo.append(2, '\0');
	appendle(geo, length, w);
	if (!fade_in)
		appendle(geo, length, w);
	geo += (char)(fade_in ? 3 : 2);
	geo += (char)shape;
	geo.append(fade_in ? 10 : 13, '\0');
	std::string rec((const char *)&_ptfunxored[model->offset - 7], 3);
	appendle(rec, 2 + geo.size(), 4);
	rec.append((const char *)&_ptfunxored[model->offset], 2);
	rec += geo;

	/* The playlist entry: a copy of the region's (a 49-byte 0x1050 ending
	 * in 00 01 01, the only layout seen in PT12 sessions), with the
	 * fade's index, where it is stored (its start for a fade-in, its end
	 * for a fade-out), 01 at +15 as in most fades Pro Tools writes, and
	 * the fade flag (the first of the last 3 bytes) set. It goes just
	 * before the region's entry for a fade-in, just after for a
	 * fade-out. */
	std::string entry((const char *)&_ptfunxored[slots[self].start], slots[self].end - slots[self].start);
	uint32_t p = e + 2 - slots[self].start;	/* payload of the 0x104f */
	uint32_t flag = entry.size() - 3;	/* first of the 3 bytes after it */
	if (entry.size() != 7 + 49 || p + 35 > entry.size()
			|| entry.compare(flag, 3, std::string("\0\1\1", 3)) != 0)
		return false;
	for (int k = 0; k < 4; k++)
		entry[p + 2 + k] = (char)((count >> (8 * k)) & 0xff);
	for (int k = 0; k < 8; k++)
		entry[p + 7 + k] = (char)(((uint64_t)edge >> (8 * k)) & 0xff);
	/* The mute stays the region's, as in the fades Pro Tools writes. */
	entry[p + 15] = 0x01;
	entry[flag] = 0x01;

	uint32_t pl = list->offset + 2;
	uint32_t countat = pl + 4 + u_endian_read4(&_ptfunxored[pl], false);
	if (countat + 4 > list->offset + list->block_size
			|| u_endian_read4(&_ptfunxored[countat], false) != slots.size())
		return false;

	std::string saved((const char *)_ptfunxored, _len);

	/* 1. The record at the end of the fade list. */
	uint32_t at = fades->offset + fades->block_size;
	std::vector<const block_t*> grow(1, fades);
	std::vector<std::pair<uint32_t, uint32_t> > set;
	set.push_back(std::make_pair(fades->offset + 2, count + 1));
	if (!splice(at, 0, rec, grow, set))
		return false;
	if (e >= at)
		e += rec.size();

	/* 2. The entry in the playlist. */
	bool ok = playlist_of(e, list, slots, self);
	if (ok) {
		pl = list->offset + 2;
		countat = pl + 4 + u_endian_read4(&_ptfunxored[pl], false);
		const block_t *map = NULL;
		for (std::vector<block_t>::const_iterator m = blocks.begin(); m != blocks.end() && !map; ++m) {
			if (m->content_type != 0x1054)
				continue;
			for (std::vector<block_t>::const_iterator l = m->child.begin(); l != m->child.end(); ++l) {
				if (&*l == list)
					map = &*m;
			}
		}
		ok = map != NULL;
		if (ok) {
			std::vector<const block_t*> grow2;
			grow2.push_back(list);
			grow2.push_back(map);
			std::vector<std::pair<uint32_t, uint32_t> > set2;
			set2.push_back(std::make_pair(countat, (uint32_t)slots.size() + 1));
			ok = splice(fade_in ? slots[self].start : slots[self].end, 0, entry, grow2, set2);
		}
	}
	if (!ok) {
		unsigned char *buf = (unsigned char *)malloc(saved.size());
		if (buf) {
			memcpy(buf, saved.data(), saved.size());
			free(_ptfunxored);
			_ptfunxored = buf;
			_len = saved.size();
		}
		reparse();
		return false;
	}
	return true;
}

bool
PTFFormat::set_clip_gain(const region_t& region, float db) {
	if (!_ptfunxored || db != db || db == HUGE_VALF)
		return false;
	uint32_t bits;
	if (db == -HUGE_VALF) {
		bits = 0xc3911af4;	/* -inf as Pro Tools stores it, per pt_api */
	} else {
		memcpy(&bits, &db, 4);
	}

	/* The single top-level 0x2637: a count and records of 8 + size bytes;
	 * only the one-point record, whose first 18 bytes are fixed, is
	 * handled. */
	static const unsigned char point[18] = { 0x01, 0x46, 0x01, 0x00, 0x16, 0, 0, 0,
		0, 0, 1, 0, 0, 0, 4, 0, 0, 0 };
	const block_t *gains = NULL;
	for (std::vector<block_t>::const_iterator b = blocks.begin(); b != blocks.end(); ++b) {
		if (b->content_type != 0x2637)
			continue;
		if (gains)
			return false;
		gains = &*b;
	}
	if (!gains || gains->block_size < 6)
		return false;
	uint32_t end = gains->offset + gains->block_size;
	uint32_t count = u_endian_read4(&_ptfunxored[gains->offset + 2], false);
	std::vector<uint32_t> records;
	uint32_t pos = gains->offset + 6;
	for (uint32_t k = 0; k < count; k++) {
		if (end - pos < 8)
			return false;
		uint32_t size = u_endian_read4(&_ptfunxored[pos + 4], false);
		if (size > end - pos - 8)
			return false;
		records.push_back(pos);
		pos += 8 + size;
	}
	if (pos != end)
		return false;

	/* Every region's point index, to know whether the target's point is
	 * shared. Clip groups also refer to points: refuse sessions with
	 * any. */
	for (std::vector<block_t>::const_iterator b = blocks.begin(); b != blocks.end(); ++b) {
		if (b->content_type == 0x262c && !b->child.empty())
			return false;
	}
	const block_t *list, *def, *info;
	if (!region_blocks(region.index, list, def, info))
		return false;
	uint32_t field = gain_field(*info);
	if (!field)
		return false;
	int32_t target = (int32_t)u_endian_read4(&_ptfunxored[field], false);
	if (target < -1 || target >= (int32_t)records.size())
		return false;
	uint32_t users = 0;
	for (uint16_t i = 0; target >= 0; i++) {
		const block_t *l, *d, *f;
		if (!region_blocks(i, l, d, f))
			break;
		uint32_t g = gain_field(*f);
		if (g && (int32_t)u_endian_read4(&_ptfunxored[g], false) == target)
			users++;
	}

	unsigned char value[4];
	write4le(value, bits);
	if (target >= 0) {
		uint32_t r = records[target];
		if (memcmp(&_ptfunxored[r], point, 18) != 0)
			return false;
		if (users == 1) {
			/* Only this region uses the point: change it in place. */
			memcpy(&_ptfunxored[r + 26], value, 4);
			return reparse();
		}
	}

	/* Add a point at the end of the list and point the region at it: a
	 * copy of the shared point, or a new one with the rest zero as the
	 * pt_api notes create it. */
	std::string data;
	if (target >= 0) {
		data.assign((const char *)&_ptfunxored[records[target]], 26);
	} else {
		data.assign((const char *)point, 18);
		data.append(8, '\0');
	}
	data.append((const char *)value, 4);
	std::vector<const block_t*> grow(1, gains);
	std::vector<std::pair<uint32_t, uint32_t> > set;
	set.push_back(std::make_pair(gains->offset + 2, count + 1));
	set.push_back(std::make_pair(field, count));
	return splice(end, 0, data, grow, set);
}

/* Return values:	0            success
			-1           error decrypting pt session
			-2           error detecting pt session
			-3           incompatible pt version
			-4           error parsing pt session
*/
int
PTFFormat::load(std::string const& ptf, int64_t targetsr) {
	cleanup();
	_path = ptf;

	if (unxor(_path))
		return -1;

	if (parse_version())
		return -2;

	if (_version < 5 || _version > 12)
		return -3;

	_targetrate = targetsr;

	int err = 0;
	if ((err = parse())) {
		printf ("PARSE FAILED %d\n", err);
		return -4;
	}

	return 0;
}

bool
PTFFormat::parse_version() {
	bool failed = true;
	struct block_t b;

	if (_ptfunxored[0] != '\x03' && foundat(_ptfunxored, 0x100, BITCODE) != 1) {
		return failed;
	}

	is_bigendian = !!_ptfunxored[0x11];

	if (!parse_block_at(0x1f, &b, NULL, 0)) {
		_version = _ptfunxored[0x40];
		if (_version == 0) {
			_version = _ptfunxored[0x3d];
		}
		if (_version == 0) {
			_version = _ptfunxored[0x3a] + 2;
		}
		if (_version != 0)
			failed = false;
		return failed;
	} else {
		if (b.content_type == 0x0003) {
			// old
			uint16_t skip = parsestring(b.offset + 3).size() + 8;
			_version = u_endian_read4(&_ptfunxored[b.offset + 3 + skip], is_bigendian);
			failed = false;
		} else if (b.content_type == 0x2067) {
			// new
			_version = 2 + u_endian_read4(&_ptfunxored[b.offset + 20], is_bigendian);
			failed = false;
		}
		return failed;
	}
}

uint8_t
PTFFormat::gen_xor_delta(uint8_t xor_value, uint8_t mul, bool negative) const {
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
	_ratefactor = 1.0;
	if (_sessionrate != 0) {
		_ratefactor = (double)_targetrate / _sessionrate;
	}
}

bool
PTFFormat::parse_block_at(uint32_t pos, struct block_t *block, struct block_t *parent, int level) {
	struct block_t b;
	int childjump = 0;
	uint32_t i;
	uint32_t max = _len;

	/* The block header is 9 bytes. */
	if (pos > _len || _len - pos < 9)
		return false;

	if (_ptfunxored[pos] != ZMARK)
		return false;

	if (parent)
		max = parent->block_size + parent->offset;

	b.zmark = ZMARK;
	b.block_type = u_endian_read2(&_ptfunxored[pos+1], is_bigendian);
	b.block_size = u_endian_read4(&_ptfunxored[pos+3], is_bigendian);
	b.content_type = u_endian_read2(&_ptfunxored[pos+7], is_bigendian);
	b.offset = pos + 7;

	/* Compare without adding: a size near 4 GB would wrap the sum and
	 * let a block run past its parent and the file. */
	if (b.offset > max || b.block_size > max - b.offset)
		return false;
	if (b.block_type & 0xff00)
		return false;

	block->zmark = b.zmark;
	block->block_type = b.block_type;
	block->block_size = b.block_size;
	block->content_type = b.content_type;
	block->offset = b.offset;
	block->child.clear();

	/* The WAV list starts with a 4-byte file count. When its low byte is
	 * ZMARK (e.g. 602 = 0x025a) the scan would take it for a block header
	 * and swallow the WAV names, so start scanning after the count. */
	i = (block->content_type == 0x1004) ? 13 : 1;
	for (; (i < block->block_size) && (pos + i + childjump < max); i += childjump ? childjump : 1) {
		int p = pos + i;
		struct block_t bchild;
		childjump = 0;
		if (parse_block_at(p, &bchild, block, level+1)) {
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
	hexdump(&_ptfunxored[b.offset], b.block_size, level);

	for (vector<PTFFormat::block_t>::iterator c = b.child.begin();
			c != b.child.end(); ++c) {
		dump_block(*c, level + 1);
	}
}

void
PTFFormat::free_block(struct block_t& b)
{
	for (vector<PTFFormat::block_t>::iterator c = b.child.begin();
			c != b.child.end(); ++c) {
		free_block(*c);
	}

	b.child.clear();
}

void
PTFFormat::free_all_blocks(void)
{
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		free_block(*b);
	}

	blocks.clear();
}

void
PTFFormat::dump(void) {
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		dump_block(*b, 0);
	}
}

void
PTFFormat::parseblocks(void) {
	uint32_t i = 20;

	while (i < _len) {
		struct block_t b;
		if (parse_block_at(i, &b, NULL, 0)) {
			blocks.push_back(b);
		}
		i += b.block_size ? b.block_size + 7 : 1;
	}
}

int
PTFFormat::parse(void) {
	parseblocks();
#ifdef DEBUG
	dump();
#endif
	if (!parseheader())
		return -1;
	parsepaths();
	setrates();
	parsemarkers();
	parseplugins();
	parsegroups();
	parsevolumes();
	parsegains();
	parsevideo();
	parseio();
	parsetempo();
	if (_sessionrate < 44100 || _sessionrate > 192000)
		return -2;
	if (!parseaudio())
		return -3;
	if (!parserest())
		return -4;
	if (!parsemidi())
		return -5;
	return 0;
}

bool
PTFFormat::parseheader(void) {
	bool found = false;

	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x1028) {
			_sessionrate = u_endian_read4(&_ptfunxored[b->offset+4], is_bigendian);
			/* Bit depth: the byte before the rate holds 16 or 24 (24 also
			 * for 32-bit float sessions). Recent sessions end the block
			 * with 02 02 <float flag> <?> <bits>, a byte nearer or
			 * farther from the end depending on the version. */
			_sessionbitdepth = _ptfunxored[b->offset+3];
			_sessionfloat = false;
			uint32_t end = b->offset + b->block_size;
			uint32_t from = (b->block_size > 16) ? end - 16 : b->offset;
			for (uint32_t i = from; i + 5 <= end; i++) {
				const unsigned char *t = &_ptfunxored[i];
				if (t[0] == 2 && t[1] == 2 && t[2] <= 1 && t[3] <= 1 &&
						(t[4] == 16 || t[4] == 24 || t[4] == 32)) {
					_sessionfloat = (t[2] == 1);
					_sessionbitdepth = t[4];
				}
			}
			found = true;
		}
	}
	return found;
}

/* The 0x2067 block holds 11 strings (Info #1..#5, five empty ones, and
 * the session name in backups), the folders where the session was saved
 * (the first one is the volume) and its file name, then a 0x2a marker,
 * an 8-byte id and a second path, folders and file name together: for a
 * backup, the session it backs up; after a Save As, the session it was
 * saved from. Either path is left empty when it cannot be read. */
void
PTFFormat::parsepaths(void) {
	_sessionpath.clear();
	_sourcepath.clear();
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type != 0x2067)
			continue;
		uint32_t end = b->offset + b->block_size;
		uint32_t pos = b->offset + 24;
		for (int k = 0; k < 11; k++) {
			if (pos + 4 > end)
				return;
			pos += 4 + parsestring(pos).size();
		}
		if (pos + 4 > end)
			return;
		uint32_t n = u_endian_read4(&_ptfunxored[pos], is_bigendian);
		pos += 4;
		if (n == 0 || n > 64)
			return;
		std::vector<std::string> path;
		/* n folders and the file name */
		for (uint32_t k = 0; k <= n; k++) {
			if (pos + 4 > end)
				return;
			std::string s = parsestring(pos);
			if (s.empty())
				return;
			pos += 4 + s.size();
			path.push_back(s);
		}
		_sessionpath = path;

		/* Skip zero words up to the 0x2a marker and the 8-byte id. */
		while (pos + 4 <= end && u_endian_read4(&_ptfunxored[pos], is_bigendian) == 0)
			pos += 4;
		if (pos + 16 > end || u_endian_read4(&_ptfunxored[pos], is_bigendian) != 0x2a)
			return;
		pos += 12;
		n = u_endian_read4(&_ptfunxored[pos], is_bigendian);
		pos += 4;
		if (n == 0 || n > 64)
			return;
		path.clear();
		for (uint32_t k = 0; k < n; k++) {
			if (pos + 4 > end)
				return;
			std::string s = parsestring(pos);
			if (s.empty())
				return;
			pos += 4 + s.size();
			path.push_back(s);
		}
		_sourcepath = path;
		return;
	}
}

/* Read a 4-byte length and that many bytes at pos, if they fit before
 * end; advance pos past them. */
bool
PTFFormat::readstring(uint32_t& pos, uint32_t end, std::string& out) {
	if (pos > end || end - pos < 4)
		return false;
	uint32_t len = u_endian_read4(&_ptfunxored[pos], is_bigendian);
	if (len > end - pos - 4)
		return false;
	out = std::string((const char *)&_ptfunxored[pos + 4], len);
	pos += 4 + len;
	return true;
}

/* Video: the 0x270a block in 0x2032 holds the session's video format,
 * with the picture width and height at +35 and +39 and the frame rate as
 * a fraction at +43 and +47 (4 bytes each). The 0x1006 block lists the
 * video files the way 0x1004 lists the audio: a 0x103a names block (a
 * count at +7, then each name followed by a 4-character type, which is
 * zero for folders) and one 0x1005 per file whose last 52 bytes hold the
 * frame rate (a double), the length in frames, 12 zero bytes, the height
 * and width (2 bytes each), the creation time (a Windows FILETIME) and a
 * 16-byte id. */
void
PTFFormat::parsevideo(void) {
	_videofiles.clear();
	_fpsnum = 0;
	_fpsden = 0;
	_dropframe = false;
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x2032) {
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x270a && c->block_size >= 51) {
					_fpsnum = u_endian_read4(&_ptfunxored[c->offset + 43], is_bigendian);
					_fpsden = u_endian_read4(&_ptfunxored[c->offset + 47], is_bigendian);
				}
			}
		}
		if (b->content_type != 0x1006)
			continue;
		std::vector<std::string> names;
		for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
				c != b->child.end(); ++c) {
			if (c->content_type != 0x103a || c->block_size < 11 || !names.empty())
				continue;
			uint32_t end = c->offset + c->block_size;
			uint32_t n = u_endian_read4(&_ptfunxored[c->offset + 7], is_bigendian);
			uint32_t pos = c->offset + 11;
			for (uint32_t k = 0; k < n; k++) {
				std::string name;
				if (!readstring(pos, end, name) || end - pos < 9)
					break;
				/* A zero type is a folder. */
				if (u_endian_read4(&_ptfunxored[pos], is_bigendian) != 0)
					names.push_back(name);
				pos += 9;
			}
		}
		size_t i = 0;
		for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
				c != b->child.end(); ++c) {
			if (c->content_type != 0x1005 || c->block_size < 54)
				continue;
			uint32_t end = c->offset + c->block_size;
			video_t vf;
			if (i < names.size())
				vf.filename = names[i];
			i++;
			uint64_t bits = u_endian_read8(&_ptfunxored[end - 52], false);
			memcpy(&vf.fps, &bits, sizeof vf.fps);
			vf.frames = u_endian_read4(&_ptfunxored[end - 44], false);
			vf.height = u_endian_read2(&_ptfunxored[end - 28], false);
			vf.width = u_endian_read2(&_ptfunxored[end - 26], false);
			int64_t t = (int64_t)(u_endian_read8(&_ptfunxored[end - 24], false) / 10000000ULL) - 11644473600LL;
			if (t >= 631152000LL && t < 4102444800LL)
				vf.created = t;
			/* A damaged file can hold any bits here. */
			if (!(vf.fps > 0 && vf.fps < 1000))
				vf.fps = 0;
			_videofiles.push_back(vf);
		}
	}

	/* The 0x204d block holds the timecode rate as a code 42 bytes in
	 * (per the pt_api format notes, section 1.4). It takes precedence
	 * over the video format: it also tells drop frame. */
	static const struct {
		uint32_t code, num, den;
		bool drop;
	} rates[] = {
		{ 0x01, 24, 1, false }, { 0x02, 25, 1, false },
		{ 0x03, 30000, 1001, false }, { 0x05, 30000, 1001, true },
		{ 0x07, 30, 1, false }, { 0x08, 30, 1, true },
		{ 0x09, 24000, 1001, false }, { 0x0a, 50, 1, false },
		{ 0x0b, 60000, 1001, false }, { 0x0c, 60000, 1001, true },
		{ 0x0d, 60, 1, false }, { 0x0e, 60, 1, true },
		{ 0x0f, 48000, 1001, false }, { 0x10, 48, 1, false },
		{ 0x11, 100, 1, false }, { 0x12, 120000, 1001, false },
		{ 0x13, 120000, 1001, true }, { 0x14, 120, 1, false },
		{ 0x15, 120, 1, true }
	};
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type != 0x204d || b->block_size < 46)
			continue;
		uint32_t code = u_endian_read4(&_ptfunxored[b->offset + 42], is_bigendian);
		for (size_t i = 0; i < sizeof rates / sizeof rates[0]; i++) {
			if (rates[i].code == code) {
				_fpsnum = rates[i].num;
				_fpsden = rates[i].den;
				_dropframe = rates[i].drop;
			}
		}
	}
}

static bool read_id(unsigned char *buf, uint32_t pos, uint32_t end, bool bigendian, std::string& id);

/* Tempo map: the top-level 0x2028 block holds the tag "Tempo", an event
 * count and one "Const" tag per event. 11 bytes into a "Const" comes a
 * "TMS" tag; 30 bytes in, the event's position in ticks (8 bytes, offset
 * by ZERO_TICKS); 40 bytes in, the tempo as a double; 48 bytes in, the
 * ticks per quarter note. */
void
PTFFormat::parsetempo(void) {
	_tempos.clear();
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type != 0x2028 || b->block_size < 7)
			continue;
		uint32_t end = b->offset + b->block_size;
		if (memcmp(&_ptfunxored[b->offset + 2], "Tempo", 5) != 0)
			continue;
		uint32_t k = b->offset + 7;
		while (jumpto(&k, _ptfunxored, end, (const unsigned char *)"Const", 5)) {
			if (end - k < 56 || memcmp(&_ptfunxored[k + 11], "TMS", 3) != 0) {
				k += 5;
				continue;
			}
			tempo_t t;
			uint64_t ticks = u_endian_read8(&_ptfunxored[k + 30], false);
			uint64_t bits = u_endian_read8(&_ptfunxored[k + 40], false);
			memcpy(&t.bpm, &bits, sizeof t.bpm);
			t.pos = (int64_t)ticks - (int64_t)ZERO_TICKS;
			/* A damaged file can hold any bits here. */
			if (t.bpm > 0 && t.bpm < 10000)
				_tempos.push_back(t);
			k += 56;
		}
	}
}

/* I/O setup: the 0x2603 block holds one 0x2602 per output or bus path
 * and the 0x1022 block one 0x1021 per input path. Both entries have the
 * name at +4, then the channel count (4 bytes) and one 2-byte index per
 * channel, then, within 12 bytes, a 0x2a marker and the path's 8-byte
 * id. A sub-path holds a second marker and the id of its parent path 4
 * bytes after its own. */
void
PTFFormat::parseio(void) {
	_iopaths.clear();
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		uint16_t item;
		if (b->content_type == 0x2603)
			item = 0x2602;
		else if (b->content_type == 0x1022)
			item = 0x1021;
		else
			continue;
		size_t first = _iopaths.size();
		std::vector<std::string> parents;
		for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
				c != b->child.end(); ++c) {
			if (c->content_type != item || c->block_size < 12)
				continue;
			/* Only the entry's own bytes: its children follow them. */
			uint32_t end = c->child.empty() ? c->offset + c->block_size : c->child[0].offset - 7;
			uint32_t pos = c->offset + 4;
			iopath_t io;
			io.list = b->content_type;
			if (!readstring(pos, end, io.name) || end - pos < 4)
				continue;
			io.channels = u_endian_read4(&_ptfunxored[pos], is_bigendian);
			pos += 4;
			if (io.channels > 64 || io.channels * 2 > end - pos)
				continue;
			pos += io.channels * 2;
			if (!read_id(_ptfunxored, pos, end, is_bigendian, io.id))
				continue;
			/* Skip to the end of the id, then look for the parent's. */
			std::string parent;
			for (uint32_t i = pos; i < pos + 12 && i + 12 <= end; i++) {
				if (u_endian_read4(&_ptfunxored[i], is_bigendian) == 0x2a) {
					read_id(_ptfunxored, i + 12, end, is_bigendian, parent);
					break;
				}
			}
			parents.push_back(parent);
			_iopaths.push_back(io);
		}
		for (size_t i = first; i < _iopaths.size(); i++) {
			const std::string& pid = parents[i - first];
			if (pid.empty())
				continue;
			for (size_t k = first; k < _iopaths.size(); k++) {
				if (_iopaths[k].id == pid) {
					_iopaths[i].parent = _iopaths[k].name;
					break;
				}
			}
		}
	}
}

/* Fades: the 0x2630 block holds one 0x262f per fade. A track map entry
 * flagged as a fade (see parserest) carries the fade's index in that list
 * and its start position. In a 0x262f, 7 bytes in, a byte gives the sizes
 * in bytes of two values (high and low nibble); after 2 more bytes come
 * the two values and then the fade type and a second byte. For a fade-in
 * the first value is the length and the second is absent; for a fade-out
 * both hold the length; for a crossfade the second is the length and the
 * first the part before the cut. Fills length, split, type and shape. */
bool
PTFFormat::parsefade(uint32_t index, fade_t& fade) {
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type != 0x2630)
			continue;
		uint32_t n = 0;
		for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
				c != b->child.end(); ++c) {
			if (c->content_type != 0x262f)
				continue;
			if (n++ != index)
				continue;
			uint32_t end = c->offset + c->block_size;
			uint32_t pos = c->offset + 7;
			if (c->block_size < 12)
				return false;
			uint32_t n1 = _ptfunxored[pos] >> 4, n2 = _ptfunxored[pos] & 0x0f;
			pos += 3;
			if (n1 > 8 || n2 > 8 || pos + n1 + n2 + 2 > end)
				return false;
			uint64_t v1 = 0, v2 = 0;
			for (uint32_t i = 0; i < n1; i++)
				v1 |= (uint64_t)_ptfunxored[pos + i] << (8 * i);
			pos += n1;
			for (uint32_t i = 0; i < n2; i++)
				v2 |= (uint64_t)_ptfunxored[pos + i] << (8 * i);
			pos += n2;
			fade.type = _ptfunxored[pos];
			fade.shape = _ptfunxored[pos + 1];
			uint64_t length = (v2 > v1) ? v2 : v1;
			fade.length = (int64_t)(length * _ratefactor);
			fade.split = (int64_t)(v1 * _ratefactor);
			return true;
		}
	}
	return false;
}

/* Clip gain points: the 0x2637 block holds a 4-byte count and that many
 * records, each an 8-byte header (a tag and the payload length) and a
 * payload that ends with a position in the audio file (8 bytes) and the
 * gain in dB (a 4-byte float). A region block refers to its point by
 * index (see parserest). Keeps the gain of each point, in order. */
void
PTFFormat::parsegains(void) {
	_gains.clear();
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type != 0x2637 || b->block_size < 6)
			continue;
		uint32_t end = b->offset + b->block_size;
		uint32_t n = u_endian_read4(&_ptfunxored[b->offset + 2], is_bigendian);
		uint32_t pos = b->offset + 6;
		for (uint32_t k = 0; k < n && end - pos >= 8; k++) {
			uint32_t len = u_endian_read4(&_ptfunxored[pos + 4], is_bigendian);
			if (len < 12 || len > end - pos - 8)
				break;
			uint32_t bits = u_endian_read4(&_ptfunxored[pos + 8 + len - 4], is_bigendian);
			float gain;
			memcpy(&gain, &bits, sizeof gain);
			_gains.push_back(gain);
			pos += 8 + len;
		}
	}
}

/* Volumes the session uses: the 0x0f3d block holds one 0x0f3c per volume
 * with its name at +2, a 4-byte id and the path it was mounted at. */
void
PTFFormat::parsevolumes(void) {
	_volumes.clear();
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type != 0x0f3d)
			continue;
		for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
				c != b->child.end(); ++c) {
			if (c->content_type != 0x0f3c)
				continue;
			uint32_t end = c->offset + c->block_size;
			uint32_t pos = c->offset + 2;
			volume_t vol;
			if (!readstring(pos, end, vol.name))
				continue;
			pos += 4;
			if (!readstring(pos, end, vol.path))
				vol.path.clear();
			_volumes.push_back(vol);
		}
	}
}

/* Memory locations: the 0x2030 block holds one 0x2077 block per
 * location, with its number (2 bytes) at +2, its name at +8 and then its
 * start and end in samples (8 bytes each). */
void
PTFFormat::parsemarkers(void) {
	_markers.clear();
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type != 0x2030)
			continue;
		for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
				c != b->child.end(); ++c) {
			if (c->content_type != 0x2077 || c->block_size < 8)
				continue;
			uint32_t end = c->offset + c->block_size;
			uint32_t pos = c->offset + 8;
			marker_t m;
			m.index = u_endian_read2(&_ptfunxored[c->offset + 2], is_bigendian);
			if (!readstring(pos, end, m.name) || end - pos < 16)
				continue;
			m.start = (int64_t)(u_endian_read8(&_ptfunxored[pos], is_bigendian) * _ratefactor);
			m.end = (int64_t)(u_endian_read8(&_ptfunxored[pos + 8], is_bigendian) * _ratefactor);
			_markers.push_back(m);
		}
	}
}

/* Groups: the 0x202b block holds a group count (4 bytes) and then that
 * many 0x202a blocks, twice (the copies were identical in every session
 * seen). A 0x202a holds its name at +2, 7 more bytes, the member count
 * (4 bytes) and one 2-byte track number per member. A track number is
 * the track's position in the 0x2519 track list, from 0: the <ALL>
 * group lists 0 to n-1 for the n tracks of that list in every session
 * seen. */
void
PTFFormat::parsegroups(void) {
	_groups.clear();
	std::vector<std::string> names;
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type != 0x2519)
			continue;
		for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
				c != b->child.end(); ++c) {
			if (c->content_type != 0x251a)
				continue;
			uint32_t pos = c->offset + 4;
			std::string name;
			if (!readstring(pos, c->offset + c->block_size, name))
				break;
			names.push_back(name);
		}
		break;
	}
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type != 0x202b || b->block_size < 6)
			continue;
		uint32_t count = u_endian_read4(&_ptfunxored[b->offset + 2], is_bigendian);
		for (uint32_t i = 0; i < count && i < b->child.size(); i++) {
			const block_t& g = b->child[i];
			if (g.content_type != 0x202a)
				break;
			uint32_t end = g.offset + g.block_size;
			uint32_t pos = g.offset + 2;
			group_t gr;
			if (!readstring(pos, end, gr.name) || end - pos < 11)
				continue;
			pos += 7;
			uint32_t n = u_endian_read4(&_ptfunxored[pos], is_bigendian);
			pos += 4;
			if (n > (end - pos) / 2)
				continue;
			for (uint32_t k = 0; k < n; k++) {
				uint16_t t = u_endian_read2(&_ptfunxored[pos + 2 * k], is_bigendian);
				if (t < names.size())
					gr.tracks.push_back(names[t]);
			}
			_groups.push_back(gr);
		}
		break;
	}
}

/* A strip's input: the 0x261b's own bytes after its last 0x260d slot
 * (after 2 zero bytes in older saves) hold 01, a byte, a 2-byte count n,
 * n 4-byte numbers, the 9 bytes 00 00 00 00 08 00 00 00 00 and then the
 * input name; the input path's 0x2a marker and id follow the name when
 * the path is in the I/O setup. An empty name means no input. */
void
PTFFormat::read_input(const block_t& strip, std::string& input)
{
	static const unsigned char gap[9] = { 0, 0, 0, 0, 8, 0, 0, 0, 0 };
	size_t last = strip.child.size();
	for (size_t k = 0; k < strip.child.size(); k++) {
		if (strip.child[k].content_type == 0x260d)
			last = k;
	}
	if (last == strip.child.size())
		return;
	const block_t& slot = strip.child[last];
	uint32_t pos = slot.offset + slot.block_size;
	uint32_t end = (last + 1 < strip.child.size()) ? strip.child[last + 1].offset - 7 : strip.offset + strip.block_size;
	if (pos > end)
		return;
	if (end - pos >= 4 && _ptfunxored[pos] == 0 && _ptfunxored[pos + 1] == 0 && _ptfunxored[pos + 2] == 1)
		pos += 2;
	if (end - pos < 4 || _ptfunxored[pos] != 1)
		return;
	uint32_t n = u_endian_read2(&_ptfunxored[pos + 2], is_bigendian);
	if (n > 64 || 4 + 4 * n + 9 > end - pos)
		return;
	pos += 4 + 4 * n;
	if (memcmp(&_ptfunxored[pos], gap, 9) != 0)
		return;
	pos += 9;
	readstring(pos, end, input);
}

/* The plugin catalog is the 0x1018 block: one 0x1017 per plugin with its
 * name at +3, the 12 bytes of its three AAX codes right after it, and its
 * bundle id 7 bytes later. Inserts are in the mixer (0x2624): each strip
 * block (0x261c, 0x261e, ...) has a 0x261b with the strip name in
 * 0x102d/0x2619 and the insert slots in 0x2627, where each slot is a
 * 0x2616 (plugin) or a 0x2625 (empty). A 0x2616 holds the same three
 * codes with each one's bytes reversed. The same 0x261b holds the strip's
 * output and sends (see below).
 *
 * The full track list is the 0x2519 block: one 0x251a per track with its
 * type (2 bytes) at +2 and its name at +4, then, within 12 bytes, a 0x2a
 * marker and an 8-byte id. The list holds every track twice. A strip's
 * 0x2619 holds the same marker and id after its own name, which links
 * the strip to its track. */
static bool
read_id(unsigned char *buf, uint32_t pos, uint32_t end, bool bigendian, std::string& id)
{
	static const char hexdigits[] = "0123456789abcdef";
	for (uint32_t i = pos; i < pos + 12 && i + 12 <= end; i++) {
		if (u_endian_read4(&buf[i], bigendian) != 0x2a)
			continue;
		id.clear();
		for (uint32_t k = 4; k < 12; k++) {
			id += hexdigits[buf[i + k] >> 4];
			id += hexdigits[buf[i + k] & 0x0f];
		}
		return true;
	}
	return false;
}

/* An automation lane (a 0x260a block): the id 01 46 01 00, its size
 * (payload minus 10), 2 zero bytes, the point count (4 bytes) and 8 more
 * bytes; then one 6-byte point per count (position in samples, 4 bytes,
 * and a signed value, 2 bytes) and 2 zero bytes. Fills points with each
 * value divided by scale; false when the block does not match. */
static bool
read_lane(unsigned char *buf, uint32_t offset, uint32_t block_size, bool bigendian, double ratefactor,
		float scale, std::vector<PTFFormat::autopoint_t>& points)
{
	if (block_size < 26)
		return false;
	uint32_t pl = offset + 2, len = block_size - 2;
	if (u_endian_read4(&buf[pl], bigendian) != 0x00014601)
		return false;
	uint32_t n = u_endian_read4(&buf[pl + 10], bigendian);
	if (n > (len - 24) / 6 || len != 24 + 6 * n)
		return false;
	for (uint32_t i = 0; i < n; i++) {
		PTFFormat::autopoint_t pt;
		pt.pos = (int64_t)(u_endian_read4(&buf[pl + 22 + 6 * i], bigendian) * ratefactor);
		pt.value = (int16_t)u_endian_read2(&buf[pl + 26 + 6 * i], bigendian) / scale;
		points.push_back(pt);
	}
	return true;
}

void
PTFFormat::parseplugins(void) {
	_plugins.clear();
	_inserts.clear();
	_routes.clear();
	_strips.clear();
	_automation.clear();
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type != 0x2519)
			continue;
		for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
				c != b->child.end(); ++c) {
			if (c->content_type != 0x251a || c->block_size < 8)
				continue;
			uint32_t end = c->offset + c->block_size;
			uint32_t pos = c->offset + 4;
			strip_t st;
			st.type = u_endian_read2(&_ptfunxored[c->offset + 2], is_bigendian);
			if (!readstring(pos, end, st.name) || !read_id(_ptfunxored, pos, end, is_bigendian, st.id))
				continue;
			/* The channel format code follows the name. */
			if (end - pos >= 2)
				st.format = u_endian_read2(&_ptfunxored[pos], is_bigendian);
			/* The display position follows the 0x2a marker and the id. */
			for (uint32_t i = pos; i < pos + 12 && i + 14 <= end; i++) {
				if (u_endian_read4(&_ptfunxored[i], is_bigendian) != 0x2a)
					continue;
				st.order = u_endian_read2(&_ptfunxored[i + 12], is_bigendian);
				break;
			}
			/* Skip the second copy of the list. */
			bool seen = false;
			for (vector<strip_t>::const_iterator o = _strips.begin(); o != _strips.end(); ++o) {
				if (o->id == st.id)
					seen = true;
			}
			if (!seen)
				_strips.push_back(st);
		}
	}
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type != 0x1018)
			continue;
		for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
				c != b->child.end(); ++c) {
			if (c->content_type != 0x1017)
				continue;
			uint32_t end = c->offset + c->block_size;
			uint32_t pos = c->offset + 3;
			plugin_t pl;
			if (!readstring(pos, end, pl.name) || end - pos < 19)
				continue;
			pl.codes = std::string((const char *)&_ptfunxored[pos], 12);
			pos += 19;
			if (!readstring(pos, end, pl.bundle))
				pl.bundle.clear();
			_plugins.push_back(pl);
		}
	}

	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type != 0x2624)
			continue;
		uint16_t strip_index = 0;
		for (vector<PTFFormat::block_t>::iterator s = b->child.begin();
				s != b->child.end(); ++s) {
			for (vector<PTFFormat::block_t>::iterator x = s->child.begin();
					x != s->child.end(); ++x) {
				if (x->content_type != 0x261b)
					continue;
				uint16_t sidx = strip_index++;
				std::string strip;
				strip_t *track = NULL;
				for (vector<PTFFormat::block_t>::iterator y = x->child.begin();
						y != x->child.end(); ++y) {
					if (y->content_type != 0x102d)
						continue;
					for (vector<PTFFormat::block_t>::iterator z = y->child.begin();
							z != y->child.end(); ++z) {
						if (z->content_type == 0x2619) {
							uint32_t zend = z->offset + z->block_size;
							uint32_t pos = z->offset + 2;
							std::string id;
							if (!readstring(pos, zend, strip) || !read_id(_ptfunxored, pos, zend, is_bigendian, id))
								continue;
							/* The strip stores its playlist name; take the
							 * track name from the track list. */
							for (vector<strip_t>::iterator t = _strips.begin(); t != _strips.end(); ++t) {
								if (t->id == id) {
									t->playlist = strip;
									strip = t->name;
									track = &*t;
									break;
								}
							}
						}
					}
				}
				if (track)
					read_input(*x, track->input);
				/* Output and sends: one 0x260d per slot, in order; an
				 * assigned slot has a 0x260e with its destination name
				 * 38 bytes in. */
				uint16_t rslot = 0;
				for (vector<PTFFormat::block_t>::iterator y = x->child.begin();
						y != x->child.end(); ++y) {
					if (y->content_type != 0x260d)
						continue;
					/* The slot's 0x1029 holds its level in tenths of dB
					 * at +3, a flag at +7 and pans at +15 and +43. */
					route_t settings;
					for (vector<PTFFormat::block_t>::iterator z = y->child.begin();
							z != y->child.end(); ++z) {
						if (z->content_type != 0x1029 || z->block_size < 47)
							continue;
						settings.level = (int32_t)u_endian_read4(&_ptfunxored[z->offset + 3], is_bigendian) / 10.0f;
						settings.muted = (_ptfunxored[z->offset + 7] != 0);
						settings.pan = (int32_t)u_endian_read4(&_ptfunxored[z->offset + 15], is_bigendian);
						settings.pan_right = (int32_t)u_endian_read4(&_ptfunxored[z->offset + 43], is_bigendian);
						break;
					}
					for (vector<PTFFormat::block_t>::iterator z = y->child.begin();
							z != y->child.end(); ++z) {
						if (z->content_type != 0x260e || z->block_size < 42)
							continue;
						route_t rt = settings;
						uint32_t pos = z->offset + 38;
						if (!readstring(pos, z->offset + z->block_size, rt.destination))
							continue;
						rt.strip = strip;
						rt.strip_index = sidx;
						rt.strip_type = s->content_type;
						rt.slot = rslot;
						rt.block = y->offset;
						_routes.push_back(rt);
					}
					/* Automation lanes: the 0x260a blocks directly under
					 * the slot (see read_lane). Lane 0 is the level in
					 * tenths of dB and lane 1 the mute, 0 or 1, the same
					 * value the flag of the 0x1029 holds. */
					uint16_t lane = 0;
					for (vector<PTFFormat::block_t>::iterator z = y->child.begin();
							z != y->child.end(); ++z) {
						if (z->content_type != 0x260a)
							continue;
						automation_t au;
						au.lane = lane++;
						if (!read_lane(_ptfunxored, z->offset, z->block_size, is_bigendian, _ratefactor, au.lane == 1 ? 1.0f : 10.0f, au.points))
							continue;
						au.strip = strip;
						au.strip_index = sidx;
						au.strip_type = s->content_type;
						au.slot = rslot;
						_automation.push_back(au);
					}
					/* Pan: up to two 0x260c under the slot (left or mono,
					 * then right), each with its own 0x260a lanes. The
					 * first lane is the pan position, -100 to 100, the
					 * same value the 0x1029 holds. */
					uint8_t side = 0;
					for (vector<PTFFormat::block_t>::iterator z = y->child.begin();
							z != y->child.end(); ++z) {
						if (z->content_type != 0x260c)
							continue;
						if (side < 2)
							side++;
						for (vector<PTFFormat::block_t>::iterator w = z->child.begin();
								w != z->child.end(); ++w) {
							if (w->content_type != 0x260a)
								continue;
							automation_t au;
							if (read_lane(_ptfunxored, w->offset, w->block_size, is_bigendian, _ratefactor, 1.0f, au.points)) {
								au.strip = strip;
								au.strip_index = sidx;
								au.strip_type = s->content_type;
								au.slot = rslot;
								au.pan = side;
								_automation.push_back(au);
							}
							break;
						}
					}
					rslot++;
				}
				for (vector<PTFFormat::block_t>::iterator y = x->child.begin();
						y != x->child.end(); ++y) {
					if (y->content_type != 0x2627)
						continue;
					uint16_t slot = 0;
					for (vector<PTFFormat::block_t>::iterator i = y->child.begin();
							i != y->child.end(); ++i) {
						if (i->content_type == 0x2616) {
							insert_t ins;
							ins.strip = strip;
							ins.strip_index = sidx;
							ins.strip_type = s->content_type;
							ins.slot = slot;
							/* Find the reversed codes of a catalog plugin. */
							uint32_t end = i->offset + i->block_size;
							for (vector<plugin_t>::const_iterator pl = _plugins.begin();
									pl != _plugins.end() && ins.plugin.name.empty(); ++pl) {
								if (pl->codes.size() != 12)
									continue;
								unsigned char want[12];
								for (int k = 0; k < 3; k++)
									for (int q = 0; q < 4; q++)
										want[k * 4 + q] = pl->codes[k * 4 + 3 - q];
								for (uint32_t at = i->offset; at + 12 <= end; at++) {
									if (memcmp(&_ptfunxored[at], want, 12) == 0) {
										ins.plugin = *pl;
										break;
									}
								}
							}
							_inserts.push_back(ins);
						}
						if (i->content_type == 0x2616 || i->content_type == 0x2625)
							slot++;
					}
				}
			}
		}
	}
}

std::string
PTFFormat::parsestring (uint32_t pos) {
	/* A damaged file can hold any length; never read past the buffer. */
	if (pos > _len || _len - pos < 4)
		return std::string();
	uint32_t length = u_endian_read4(&_ptfunxored[pos], is_bigendian);
	pos += 4;
	if (length > _len - pos)
		return std::string();
	return std::string((const char *)&_ptfunxored[pos], length);
}

bool
PTFFormat::parseaudio(void) {
	bool found = false;
	uint32_t nwavs = 0;
	uint32_t n;
	uint32_t pos = 0;
	std::string wavtype;
	std::string wavname;

	// Parse wav names
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x1004) {

			nwavs = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);

			/* The count above can be lower than the number of files: a
			 * session with ADM BWF files has a second WAV names block whose
			 * files are not counted. Every file has a metadata entry, so
			 * bound the names by those entries as well. */
			uint32_t nentries = 0;
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1003)
					nentries++;
			}
			uint32_t maxwavs = (nentries > nwavs) ? nentries : nwavs;

			/* Number files across all names blocks so indices stay unique. */
			n = 0;
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x103a) {
					//nstrings = u_endian_read4(&_ptfunxored[c->offset+1], is_bigendian);
					pos = c->offset + 11;
					// Found wav list
					for (; (pos < c->offset + c->block_size) && (n < maxwavs);) {
						wavname = parsestring(pos);
						pos += wavname.size() + 4;
						wavtype = std::string((const char*)&_ptfunxored[pos], 4);
						pos += 9;
						if (foundin(wavname, std::string(".grp")))
							continue;

						if (foundin(wavname, std::string("Audio Files"))) {
							continue;
						}
						if (foundin(wavname, std::string("Fade Files"))) {
							continue;
						}
						if (_version < 10) {
							if (!(foundin(wavtype, std::string("WAVE")) ||
									foundin(wavtype, std::string("EVAW")) ||
									foundin(wavtype, std::string("AIFF")) ||
									foundin(wavtype, std::string("FFIA")) ||
									foundin(wavtype, std::string("fFXM")) ||
									foundin(wavtype, std::string("MXFf"))) ) {
								continue;
							}
						} else {
							if (wavtype[0] != '\0') {
								if (!(foundin(wavtype, std::string("WAVE")) ||
										foundin(wavtype, std::string("EVAW")) ||
										foundin(wavtype, std::string("AIFF")) ||
										foundin(wavtype, std::string("FFIA")) ||
										foundin(wavtype, std::string("fFXM")) ||
										foundin(wavtype, std::string("MXFf"))) ) {
									continue;
								}
							} else if (!(foundin(wavname, std::string(".wav")) ||
									foundin(wavname, std::string(".aif")) ||
									foundin(wavname, std::string(".mxf"))) ) {
								continue;
							}
						}
						found = true;
						wav_t f (n);
						f.filename = wavname;
						n++;
						_audiofiles.push_back(f);
					}
				}
			}
		}
	}

	if (!found) {
		if (nwavs > 0) {
			return false;
		} else {
			return true;
		}
	}

	// Add wav length information
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x1004) {

			vector<PTFFormat::wav_t>::iterator wav = _audiofiles.begin();

			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1003) {
					/* The 0x2106 metadata of a file holds its creation time
					 * as a Windows FILETIME (100 ns units since 1601) 31
					 * bytes into the block. */
					int64_t created = 0;
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if (d->content_type == 0x2106 && d->block_size >= 39) {
							uint64_t ft = u_endian_read8(&_ptfunxored[d->offset+31], false);
							int64_t t = (int64_t)(ft / 10000000ULL) - 11644473600LL;
							/* Keep 1990..2100 only; other values are not dates. */
							if (t >= 631152000LL && t < 4102444800LL)
								created = t;
						}
					}
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if (d->content_type == 0x1001) {
							if (wav == _audiofiles.end())
								break;
							(*wav).length = u_endian_read8(&_ptfunxored[d->offset+8], is_bigendian);
							(*wav).created = created;
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
		offsetbytes = (_ptfunxored[j+4] & 0xf0) >> 4;
		lengthbytes = (_ptfunxored[j+3] & 0xf0) >> 4;
		startbytes = (_ptfunxored[j+2] & 0xf0) >> 4;
		//somethingbytes = (_ptfunxored[j+2] & 0xf);
		//skipbytes = _ptfunxored[j+1];
	} else {
		offsetbytes = (_ptfunxored[j+1] & 0xf0) >> 4; //3
		lengthbytes = (_ptfunxored[j+2] & 0xf0) >> 4;
		startbytes = (_ptfunxored[j+3] & 0xf0) >> 4; //1
		//somethingbytes = (_ptfunxored[j+3] & 0xf);
		//skipbytes = _ptfunxored[j+4];
	}

	switch (offsetbytes) {
	case 5:
		offset = u_endian_read5(&_ptfunxored[j+5], false);
		break;
	case 4:
		offset = (uint64_t)u_endian_read4(&_ptfunxored[j+5], false);
		break;
	case 3:
		offset = (uint64_t)u_endian_read3(&_ptfunxored[j+5], false);
		break;
	case 2:
		offset = (uint64_t)u_endian_read2(&_ptfunxored[j+5], false);
		break;
	case 1:
		offset = (uint64_t)(_ptfunxored[j+5]);
		break;
	default:
		offset = 0;
		break;
	}
	j+=offsetbytes;
	switch (lengthbytes) {
	case 5:
		length = u_endian_read5(&_ptfunxored[j+5], false);
		break;
	case 4:
		length = (uint64_t)u_endian_read4(&_ptfunxored[j+5], false);
		break;
	case 3:
		length = (uint64_t)u_endian_read3(&_ptfunxored[j+5], false);
		break;
	case 2:
		length = (uint64_t)u_endian_read2(&_ptfunxored[j+5], false);
		break;
	case 1:
		length = (uint64_t)(_ptfunxored[j+5]);
		break;
	default:
		length = 0;
		break;
	}
	j+=lengthbytes;
	switch (startbytes) {
	case 5:
		start = u_endian_read5(&_ptfunxored[j+5], false);
		break;
	case 4:
		start = (uint64_t)u_endian_read4(&_ptfunxored[j+5], false);
		break;
	case 3:
		start = (uint64_t)u_endian_read3(&_ptfunxored[j+5], false);
		break;
	case 2:
		start = (uint64_t)u_endian_read2(&_ptfunxored[j+5], false);
		break;
	case 1:
		start = (uint64_t)(_ptfunxored[j+5]);
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

	findex = u_endian_read4(&_ptfunxored[blk.offset + blk.block_size], is_bigendian);
	/* From PT10 on, the file index is followed by the 2-byte channel of
	 * that file the region plays (0 = L, 1 = R, ...), then two floats.
	 * Older versions store other data there. */
	r.channel = (_version >= 10)
		? u_endian_read2(&_ptfunxored[blk.offset + blk.block_size + 4], is_bigendian)
		: 0;
	/* PT12 sessions follow the two floats with 8 zero bytes, 0xff and a
	 * 16-byte UUID per region that persists across saves. */
	uint32_t u = blk.offset + blk.block_size + 22;
	r.uid.clear();
	if (_version >= 10 && u + 17 <= _len && _ptfunxored[u] == 0xff) {
		static const char hexdigits[] = "0123456789abcdef";
		for (uint32_t k = 1; k <= 16; k++) {
			r.uid += hexdigits[_ptfunxored[u + k] >> 4];
			r.uid += hexdigits[_ptfunxored[u + k] & 0x0f];
		}
	}
	wav_t f (findex);
	f.posabsolute = start * _ratefactor;
	f.length = length * _ratefactor;

	wav_t found;
	if (find_wav(findex, found)) {
		f.filename = found.filename;
		f.created = found.created;
	}

	std::vector<midi_ev_t> m;
	r.startpos = (int64_t)(start*_ratefactor);
	r.sampleoffset = (int64_t)(sampleoffset*_ratefactor);
	r.length = (int64_t)(length*_ratefactor);
	r.wave = f;
	r.midi = m;
}

bool
PTFFormat::parserest(void) {
	uint32_t i, j, count;
	uint64_t start;
	uint16_t rindex, rawindex, tindex, mindex;
	uint32_t nch;
	bool found = false;
	bool region_is_fade = false;
	std::string regionname, trackname, midiregionname;
	rindex = 0;

	// Parse sources->regions
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x100b || b->content_type == 0x262a) {
			//nregions = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1008 || c->content_type == 0x2629) {
					/* The region info is in the first child block. */
					if (c->child.empty())
						continue;
					vector<PTFFormat::block_t>::iterator d = c->child.begin();
					region_t r;

					found = true;
					j = c->offset + 11;
					regionname = parsestring(j);
					j += regionname.size() + 4;

					r.name = regionname;
					r.index = rindex;
					parse_region_info(j, *d, r);

					/* The first child of a PT10+ region ends with: a 0x2a
					 * marker and the 8-byte id of the track the region was
					 * recorded on (only for audio recorded in the session),
					 * one byte, the index of the region's clip gain point
					 * (0xffffffff when it has none) and two bytes. Newer
					 * sessions append a copy of the region's UUID, which
					 * moves all of that 16 bytes away from the end. */
					if (c->content_type == 0x2629) {
						uint32_t tail = d->offset + d->block_size;
						if (d->block_size >= 16 && tail + 39 <= _len && _ptfunxored[tail + 22] == 0xff &&
								memcmp(&_ptfunxored[tail - 16], &_ptfunxored[tail + 23], 16) == 0)
							tail -= 16;
						if (tail - d->offset >= 19) {
							uint32_t gi = u_endian_read4(&_ptfunxored[tail - 6], is_bigendian);
							if (gi < _gains.size())
								r.gain = _gains[gi];
							std::string id;
							if (read_id(_ptfunxored, tail - 19, tail - 7, is_bigendian, id)) {
								for (vector<strip_t>::const_iterator t = _strips.begin(); t != _strips.end(); ++t) {
									if (t->id == id) {
										r.recorded_on = t->name;
										break;
									}
								}
							}
						}
					}

					/* Regions of ADM BWF files store 0xffffffff as their file
					 * index. Recent PT12 sessions end every region block with
					 * 0x2a, an 8-byte id, 0x01 and the file index, so take it
					 * from there when it names a known file. */
					uint32_t end = c->offset + c->block_size;
					if (c->content_type == 0x2629 &&
							u_endian_read4(&_ptfunxored[d->offset + d->block_size], is_bigendian) == 0xffffffff &&
							c->block_size >= 21 &&
							u_endian_read4(&_ptfunxored[end - 21], is_bigendian) == 0x2a &&
							_ptfunxored[end - 9] == 0x01) {
						wav_t found;
						uint32_t tindex = u_endian_read4(&_ptfunxored[end - 8], is_bigendian);
						if (tindex < 0xffff && find_wav(tindex, found)) {
							r.wave.index = found.index;
							r.wave.filename = found.filename;
							r.wave.created = found.created;
						}
					}

					_regions.push_back(r);
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
			//ntracks = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1014) {
					j = c->offset + 2;
					trackname = parsestring(j);
					j += trackname.size() + 5;
					nch = u_endian_read4(&_ptfunxored[j], is_bigendian);
					j += 4;
					/* Stop at the end of the block: a damaged count must not
					 * read past it. */
					for (i = 0; i < nch && j + 2 <= c->offset + c->block_size; i++) {
						uint16_t ch = u_endian_read2(&_ptfunxored[j], is_bigendian);

						track_t ti;
						if (!find_track(ch, ti)) {
							// Add a dummy region for now
							region_t r (65535);
							track_t t (ch);
							t.name = trackname;
							t.reg = r;
							_tracks.push_back(t);
						}
						//verbose_printf("%s : %d(%d)\n", reg, nch, ch);
						j += 2;
					}
				}
			}
		}
	}

	// Reparse from scratch to exclude audio tracks from all tracks to get midi tracks
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x2519) {
			tindex = 0;
			mindex = 0;
			//ntracks = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x251a) {
					j = c->offset + 4;
					trackname = parsestring(j);
					j += trackname.size() + 4 + 18;
					//tindex = u_endian_read4(&_ptfunxored[j], is_bigendian);

					// Add a dummy region for now
					region_t r (65535);
					track_t t (mindex);
					t.name = trackname;
					t.reg = r;

					track_t ti;
					// If the current track is not an audio track, insert as midi track
					if (!(find_track(tindex, ti) && foundin(trackname, ti.name))) {
						_miditracks.push_back(t);
						mindex++;
					}
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
			//nregions = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);
			count = 0;
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1011) {
					regionname = parsestring(c->offset + 2);
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if (d->content_type == 0x100f) {
							for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
									e != d->child.end(); ++e) {
								if (e->content_type == 0x100e) {
									// Region->track
									track_t ti;
									j = e->offset + 4;
									rawindex = u_endian_read4(&_ptfunxored[j], is_bigendian);
									if (!find_track(count, ti))
										continue;
									if (!find_region(rawindex, ti.reg))
										continue;
									if (ti.reg.index != 65535) {
										_tracks.push_back(ti);
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
			//nregions = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);
			count = 0;
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1052) {
					trackname = parsestring(c->offset + 2);
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if (d->content_type == 0x1050) {
							region_is_fade = (_ptfunxored[d->offset + 46] == 0x01);
							if (region_is_fade) {
								/* A fade entry has the same layout as a region
								 * entry, with the fade's index in the fade list
								 * in place of the region index. */
								for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
										e != d->child.end(); ++e) {
									if (e->content_type != 0x104f || e->block_size < 17)
										continue;
									fade_t fd;
									if (!parsefade(u_endian_read4(&_ptfunxored[e->offset + 4], is_bigendian), fd))
										continue;
									fd.track = trackname;
									fd.trackindex = count;
									/* The stored position is where a fade-in
									 * starts, where a fade-out ends and the
									 * cut of a crossfade. */
									fd.start = (int64_t)(u_endian_read8(&_ptfunxored[e->offset + 9], is_bigendian) * _ratefactor);
									if (fd.type == 2)
										fd.start -= fd.length;
									else if (fd.type == 1)
										fd.start -= fd.split;
									_fades.push_back(fd);
								}
								continue;
							}
							for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
									e != d->child.end(); ++e) {
								if (e->content_type == 0x104f) {
									// Region->track
									j = e->offset + 4;
									rawindex = u_endian_read4(&_ptfunxored[j], is_bigendian);
									j += 4 + 1;
									start = u_endian_read4(&_ptfunxored[j], is_bigendian);
									tindex = count;
									track_t ti;
									if (!find_track(tindex, ti)) {
										verbose_printf("dropped track %d\n", tindex);
										continue;
									}
									if (!find_region(rawindex, ti.reg)) {
										verbose_printf("dropped region %d\n", rawindex);
										continue;
									}
									ti.reg.startpos = start * _ratefactor;
									/* The entry's first byte is the mute of
									 * this placement (per the pt_api format
									 * notes, section 4.2). */
									ti.reg.muted = (_ptfunxored[e->offset + 2] != 0);
									ti.entry = e->offset;
									if (ti.reg.index != 65535) {
										_tracks.push_back(ti);
									}
								}
							}
						}
					}
					found = true;
					count++;
				}
			}
		}
	}
	for (std::vector<track_t>::iterator tr = _tracks.begin();
			tr != _tracks.end(); /* noop */) {
		if ((*tr).reg.index == 65535) {
			tr = _tracks.erase(tr);
		} else {
			tr++;
		}
	}

	if (_tracks.begin() == _tracks.end())
		return found;

	/* Sort track entries by index */
	std::sort(_tracks.begin(), _tracks.end());

	/* Renumber track entries to be gapless */
	for (std::vector<track_t>::iterator tr = _tracks.begin() + 1;
			tr != _tracks.end(); tr++) {
		while ((*tr).index == (*(tr-1)).index) {
			tr++;
			if (tr == _tracks.end()) {
				break;
			}
		}
		if (tr == _tracks.end()) {
			break;
		}
		int diffn = (*tr).index - (*(tr-1)).index - 1;
		if (diffn) {
			for (std::vector<track_t>::iterator rest = tr;
					rest != _tracks.end(); rest++) {
				(*rest).index -= diffn;
			}
		}
	}

	/* Renumber track entries to be zero based */
	int first = _tracks.empty() ? 0 : _tracks[0].index;
	if (first > 0) {
		for (std::vector<track_t>::iterator tr = _tracks.begin();
				tr != _tracks.end(); tr++) {
			(*tr).index -= first;
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
	// uint32_t mindex;
	uint32_t i, j, k, n, rindex, tindex, count, rawindex;
	uint64_t n_midi_events, zero_ticks, start, offset, length, start2, stop2;
	uint64_t midi_pos, midi_len, max_pos, region_pos;
	uint8_t midi_velocity, midi_note;
	uint16_t regionnumber = 0;
	std::string midiregionname;

	std::vector<mchunk> midichunks;
	midi_ev_t m;

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

				if (!jumpto(&k, _ptfunxored, _len, (const unsigned char *)"MdNLB", 5)) {
					break;
				}
				k += 11;
				/* The count and zero ticks take 9 bytes. */
				if (k > _len || _len - k < 9)
					break;
				n_midi_events = u_endian_read4(&_ptfunxored[k], is_bigendian);

				k += 4;
				zero_ticks = u_endian_read5(&_ptfunxored[k], is_bigendian);
				/* Each event read takes 18 bytes from k. */
				for (i = 0; i < n_midi_events && k < _len && _len - k >= 18; i++, k += 35) {
					midi_pos = u_endian_read5(&_ptfunxored[k], is_bigendian);
					midi_pos -= zero_ticks;
					midi_note = _ptfunxored[k+8];
					midi_len = u_endian_read5(&_ptfunxored[k+9], is_bigendian);
					midi_velocity = _ptfunxored[k+17];

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
		} else if ((b->content_type == 0x2002) || (b->content_type == 0x2634)) {
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if ((c->content_type == 0x2001) || (c->content_type == 0x2633)) {
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if ((d->content_type == 0x1007) || (d->content_type == 0x2628)) {
							j = d->offset + 2;
							midiregionname = parsestring(j);
							j += 4 + midiregionname.size();
							parse_three_point(j, region_pos, zero_ticks, midi_len);
							j = d->offset + d->block_size;
							rindex = u_endian_read4(&_ptfunxored[j], is_bigendian);
							if (rindex >= midichunks.size())
								continue;
							struct mchunk mc = *(midichunks.begin()+rindex);

							region_t r (regionnumber++);
							r.name = midiregionname;
							r.startpos = (int64_t)0xe8d4a51000ULL;
							r.sampleoffset = 0;
							r.length = mc.maxlen;
							r.midi = mc.chunk;

							_midiregions.push_back(r);
							//verbose_printf("MIDI %s : r(%d) (%llu, %llu, %llu)\n", str, rindex, zero_ticks, region_pos, midi_len);
							//dump_block(*d, 1);
						}
					}
				}
			}
		} 
	}
	
	// COMPOUND MIDI regions
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x262c) {
			// mindex = 0;
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x262b) {
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if (d->content_type == 0x2628) {
							count = 0;
							j = d->offset + 2;
							regionname = parsestring(j);
							j += 4 + regionname.size();
							parse_three_point(j, start, offset, length);
							j = d->offset + d->block_size + 2;
							n = u_endian_read2(&_ptfunxored[j], is_bigendian);

							for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
									e != d->child.end(); ++e) {
								if (e->content_type == 0x2523) {
									// FIXME Compound MIDI region
									j = e->offset + 39;
									rawindex = u_endian_read4(&_ptfunxored[j], is_bigendian);
									j += 12; 
									start2 = u_endian_read5(&_ptfunxored[j], is_bigendian);
									int64_t signedval = (int64_t)start2;
									signedval -= ZERO_TICKS;
									if (signedval < 0) {
										signedval = -signedval;
									}
									start2 = signedval;
									j += 8;
									stop2 = u_endian_read5(&_ptfunxored[j], is_bigendian);
									signedval = (int64_t)stop2;
									signedval -= ZERO_TICKS;
									if (signedval < 0) {
										signedval = -signedval;
									}
									stop2 = signedval;
									j += 16;
									//nn = u_endian_read4(&_ptfunxored[j], is_bigendian);
									//verbose_printf("COMPOUND %s : c(%d) r(%d) ?(%d) ?(%d) (%llu %llu)(%llu %llu %llu)\n", str, mindex, rawindex, n, nn, start2, stop2, start, offset, length);
									count++;
								}
							}
							if (!count) {
								// Plain MIDI region
								struct mchunk mc = *(midichunks.begin()+n);

								region_t r (n);
								r.name = midiregionname;
								r.startpos = (int64_t)0xe8d4a51000ULL;
								r.length = mc.maxlen;
								r.midi = mc.chunk;
								_midiregions.push_back(r);
								// verbose_printf("%s : MIDI region mr(%d) ?(%d) (%lu %lu %lu)\n", regionname.c_str(), mindex, n, start, offset, length);
								// mindex++;
							}
						}
					}
				}
			}
		} 
	}
	
	// Put midi regions onto midi tracks
	for (vector<PTFFormat::block_t>::iterator b = blocks.begin();
			b != blocks.end(); ++b) {
		if (b->content_type == 0x1058) {
			//nregions = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);
			count = 0;
			for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
					c != b->child.end(); ++c) {
				if (c->content_type == 0x1057) {
					regionname = parsestring(c->offset + 2);
					for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
							d != c->child.end(); ++d) {
						if (d->content_type == 0x1056) {
							for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
									e != d->child.end(); ++e) {
								if (e->content_type == 0x104f) {
									// MIDI region->MIDI track
									track_t ti;
									j = e->offset + 4;
									rawindex = u_endian_read4(&_ptfunxored[j], is_bigendian);
									j += 4 + 1;
									start = u_endian_read5(&_ptfunxored[j], is_bigendian);
									tindex = count;
									if (!find_miditrack(tindex, ti)) {
										verbose_printf("dropped midi t(%d) r(%d)\n", tindex, rawindex);
										continue;
									}
									if (!find_midiregion(rawindex, ti.reg)) {
										verbose_printf("dropped midiregion\n");
										continue;
									}
									//verbose_printf("MIDI : %s : t(%d) r(%d) %llu(%llu)\n", ti.name.c_str(), tindex, rawindex, start, ti.reg.startpos);
									int64_t signedstart = (int64_t)(start - ZERO_TICKS);
									if (signedstart < 0)
										signedstart = -signedstart;
									ti.reg.startpos = (uint64_t)(signedstart * _ratefactor);
									if (ti.reg.index != 65535) {
										_miditracks.push_back(ti);
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
	for (std::vector<track_t>::iterator tr = _miditracks.begin();
			tr != _miditracks.end(); /* noop */) {
		if ((*tr).reg.index == 65535) {
			tr = _miditracks.erase(tr);
		} else {
			tr++;
		}
	}
	return true;
}
