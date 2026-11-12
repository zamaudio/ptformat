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
#ifndef PTFFORMAT_H
#define PTFFORMAT_H

#include <string>
#include <cstring>
#include <algorithm>
#include <vector>
#include <stdint.h>

#include "ptformat/visibility.h"

class LIBPTFORMAT_API PTFFormat {
public:
	PTFFormat();
	~PTFFormat();

	/* Return values:	0            success
				-1           error decrypting pt session
				-2           error detecting pt session
				-3           incompatible pt version
				-4           error parsing pt session
	*/
	int load(std::string const& path, int64_t targetsr);

	/* Return values:	0            success
				-1           error decrypting pt session
	*/
	int unxor(std::string const& path);

	struct wav_t {
		std::string filename;
		uint16_t    index;

		int64_t     posabsolute;
		int64_t     length;
		/* When the file was created, in seconds since 1970-01-01 UTC;
		 * 0 when the session does not hold a valid date for it (files
		 * imported rather than recorded often have none). */
		int64_t     created;

		bool operator <(const struct wav_t& other) const {
			std::string a (this->filename);
			std::string b (other.filename);
			std::transform (a.begin(), a.end(), a.begin(), ::tolower);
			std::transform (b.begin(), b.end(), b.begin(), ::tolower);
			return a < b;
		}

		bool operator ==(const struct wav_t& other) const {
			return (this->filename == other.filename ||
				this->index == other.index);
		}

		wav_t (uint16_t idx = 0) : index (idx), posabsolute (0), length (0), created (0) {}
	};

	struct midi_ev_t {
		uint64_t pos;
		uint64_t length;
		uint8_t note;
		uint8_t velocity;
		midi_ev_t () : pos (0), length (0), note (0), velocity (0) {}
	};

	struct region_t {
		std::string name;
		uint16_t    index;
		int64_t     startpos;
		int64_t     sampleoffset;
		int64_t     length;
		wav_t       wave;
		/* Channel of wave this region plays; read from PT10 on, 0 before. */
		uint32_t    channel;
		/* UUID that stays the same across saves of a session, as 32 hex
		 * digits; empty when the session does not store one (PT12
		 * sessions do, PT5 and PT8 do not, PT10 and PT11 untested). */
		std::string uid;
		/* Clip gain in dB; 0 when the region has none. Read from the
		 * single gain point the sessions seen store per region. */
		float       gain;
		/* Name of the track the region was recorded on; empty for
		 * imported audio or when the session does not tell. */
		std::string recorded_on;
		std::vector<midi_ev_t> midi;

		bool operator ==(const region_t& other) const {
			return (this->index == other.index);
		}

		bool operator <(const region_t& other) const {
			std::string a (this->name);
			std::string b (other.name);
			std::transform (a.begin(), a.end(), a.begin(), ::tolower);
			std::transform (b.begin(), b.end(), b.begin(), ::tolower);
			return a < b;
		}
		region_t (uint16_t idx = 0) : index (idx), startpos (0), sampleoffset (0), length (0), channel (0), gain (0) {}
	};

	/* A memory location: its number, name and position in samples at the
	 * target rate; start == end for a marker. */
	struct marker_t {
		uint16_t    index;
		std::string name;
		int64_t     start;
		int64_t     end;
		marker_t () : index (0), start (0), end (0) {}
	};

	/* A plugin the session uses: its name, its three 4-character AAX
	 * codes as stored in the catalog (12 characters), and its bundle id
	 * (for example com.avid.aax.trim). */
	struct plugin_t {
		std::string name;
		std::string codes;
		std::string bundle;
	};

	/* A track of any kind, from the session's full track list. type is 0
	 * for audio tracks, 1 for MIDI tracks, 2 for aux tracks, 5 for a
	 * master fader and 8 for a video track in the sessions seen (5 and 8
	 * deduced from the track names). id is the 8-byte id that links
	 * the track to its mixer strip, as 16 hex digits. playlist is the name
	 * its mixer strip stores: the track name for aux tracks, the playlist
	 * name for audio tracks (for example "Audio 1.dup1"); empty when the
	 * strip stores none or the track has no strip (video tracks). */
	struct strip_t {
		std::string name;
		uint16_t    type;
		std::string id;
		std::string playlist;
		strip_t () : type (0) {}
	};

	/* A plugin inserted in a mixer strip. slot counts from 0 (insert A).
	 * strip_index is the strip's position in the mixer, from 0. strip is
	 * the track's name, or the name the strip stores (see strip_t) when
	 * the track list does not hold the strip's id. strip_type is the
	 * strip's block type: 0x261c for audio tracks and 0x261e for aux
	 * tracks in the sessions seen. plugin is empty when the inserted
	 * plugin is not in the catalog. */
	struct insert_t {
		std::string strip;
		uint16_t    strip_index;
		uint16_t    strip_type;
		uint16_t    slot;
		plugin_t    plugin;
		insert_t () : strip_index (0), strip_type (0), slot (0) {}
	};

	/* Where a mixer strip sends its signal: the name of a destination
	 * path of one of its 11 slots (a slot can have several). In the
	 * sessions seen, slot 0 is the strip's main output and slots 1 to 10
	 * are sends A to J; this is deduced from the routing, not checked in
	 * Pro Tools. strip, strip_index and strip_type are as in insert_t.
	 *
	 * level is the slot's level in dB (stored in tenths): the track
	 * fader for slot 0, the send level for the others. muted is a flag
	 * stored with it; in a dubbing session it is set on the output of
	 * the original dialogue tracks, so it looks like mute, but what it
	 * means on a send is not known. pan and pan_right run from -100
	 * (left) to 100 (right); a stereo strip holds -100 and 100, a mono
	 * strip 0 in both. These four are deduced from the values, not
	 * checked in Pro Tools. */
	struct route_t {
		std::string strip;
		uint16_t    strip_index;
		uint16_t    strip_type;
		uint16_t    slot;
		std::string destination;
		float       level;
		bool        muted;
		int32_t     pan;
		int32_t     pan_right;
		route_t () : strip_index (0), strip_type (0), slot (0), level (0), muted (false), pan (0), pan_right (0) {}
	};

	/* A volume the session uses: its name and where it was mounted (for
	 * example "Media" and "/Volumes/Media/"). */
	struct volume_t {
		std::string name;
		std::string path;
	};

	/* A fade on an audio track. type is 3 for a fade-in (it starts where
	 * a region starts), 2 for a fade-out (it starts where a region ends)
	 * and 1 for a crossfade (where one region ends and the next starts).
	 * start and length are in samples at the target rate. For a
	 * crossfade, split is the part of length before the cut; otherwise it
	 * equals length. shape is a second byte stored with the type, whose
	 * meaning is not known. trackindex is the position of the track in
	 * the region to track map, from 0. */
	struct fade_t {
		std::string track;
		uint16_t    trackindex;
		int64_t     start;
		int64_t     length;
		int64_t     split;
		uint8_t     type;
		uint8_t     shape;
		fade_t () : trackindex (0), start (0), length (0), split (0), type (0), shape (0) {}
	};

	/* A video file the session uses: its name, frame rate, length in
	 * frames, picture size and creation time (seconds since 1970 UTC, 0
	 * when not a valid date). */
	struct video_t {
		std::string filename;
		double      fps;
		uint32_t    frames;
		uint16_t    width;
		uint16_t    height;
		int64_t     created;
		video_t () : fps (0), frames (0), width (0), height (0), created (0) {}
	};

	struct track_t {
		std::string name;
		uint16_t    index;
		uint8_t     playlist;
		region_t    reg;

		bool operator <(const track_t& other) const {
			return (this->index < other.index);
		}

		bool operator ==(const track_t& other) const {
			return (this->index == other.index);
		}
		track_t (uint16_t idx = 0) : index (idx), playlist (0) {}
	};

	bool find_track(uint16_t index, track_t& tt) const {
		std::vector<track_t>::const_iterator begin = _tracks.begin();
		std::vector<track_t>::const_iterator finish = _tracks.end();
		std::vector<track_t>::const_iterator found;

		track_t t (index);

		if ((found = std::find(begin, finish, t)) != finish) {
			tt = *found;
			return true;
		}
		return false;
	}

	bool find_region(uint16_t index, region_t& rr) const {
		std::vector<region_t>::const_iterator begin = _regions.begin();
		std::vector<region_t>::const_iterator finish = _regions.end();
		std::vector<region_t>::const_iterator found;

		region_t r;
		r.index = index;

		if ((found = std::find(begin, finish, r)) != finish) {
			rr = *found;
			return true;
		}
		return false;
	}
	
	bool find_miditrack(uint16_t index, track_t& tt) const {
		std::vector<track_t>::const_iterator begin = _miditracks.begin();
		std::vector<track_t>::const_iterator finish = _miditracks.end();
		std::vector<track_t>::const_iterator found;

		track_t t (index);

		if ((found = std::find(begin, finish, t)) != finish) {
			tt = *found;
			return true;
		}
		return false;
	}

	bool find_midiregion(uint16_t index, region_t& rr) const {
		std::vector<region_t>::const_iterator begin = _midiregions.begin();
		std::vector<region_t>::const_iterator finish = _midiregions.end();
		std::vector<region_t>::const_iterator found;

		region_t r (index);

		if ((found = std::find(begin, finish, r)) != finish) {
			rr = *found;
			return true;
		}
		return false;
	}

	bool find_wav(uint16_t index, wav_t& ww) const {
		std::vector<wav_t>::const_iterator begin = _audiofiles.begin();
		std::vector<wav_t>::const_iterator finish = _audiofiles.end();
		std::vector<wav_t>::const_iterator found;

		wav_t w (index);

		if ((found = std::find(begin, finish, w)) != finish) {
			ww = *found;
			return true;
		}
		return false;
	}

	static bool regionexistsin(std::vector<region_t> const& reg, uint16_t index) {
		std::vector<region_t>::const_iterator begin = reg.begin();
		std::vector<region_t>::const_iterator finish = reg.end();

		region_t r (index);

		if (std::find(begin, finish, r) != finish) {
			return true;
		}
		return false;
	}

	static bool wavexistsin (std::vector<wav_t> const& wv, uint16_t index) {
		std::vector<wav_t>::const_iterator begin = wv.begin();
		std::vector<wav_t>::const_iterator finish = wv.end();

		wav_t w (index);

		if (std::find(begin, finish, w) != finish) {
			return true;
		}
		return false;
	}

	uint8_t version () const { return _version; }
	int64_t sessionrate () const { return _sessionrate ; }
	/* Bits per sample of the session (16, 24 or 32), and whether it is
	 * floating point; 0 when unknown. */
	int sessionbitdepth () const { return _sessionbitdepth ; }
	bool sessionfloat () const { return _sessionfloat ; }
	/* Frame rate of the session's video format, as a fraction (for
	 * example 24000/1001); the denominator is 0 when the session holds
	 * none. In the sessions seen it is the timecode rate: track positions
	 * are samples counted from timecode 00:00:00:00. */
	uint32_t frameratenum () const { return _fpsnum ; }
	uint32_t framerateden () const { return _fpsden ; }
	/* Where the session was saved, as path components: the volume, the
	 * folders and the file name. Empty when not stored or not read. */
	const std::vector<std::string>& sessionpath () const { return _sessionpath ; }
	/* A second path in the same form: for a backup, the session it backs
	 * up; after a Save As, the session it was saved from. */
	const std::vector<std::string>& sourcepath () const { return _sourcepath ; }
	const std::string& path () { return _path; }

	const std::vector<wav_t>&    audiofiles () const { return _audiofiles ; }
	const std::vector<region_t>& regions () const { return _regions ; }
	const std::vector<region_t>& midiregions () const { return _midiregions ; }
	const std::vector<track_t>&  tracks () const { return _tracks ; }
	const std::vector<track_t>&  miditracks () const { return _miditracks ; }
	const std::vector<marker_t>& markers () const { return _markers ; }
	const std::vector<plugin_t>& plugins () const { return _plugins ; }
	const std::vector<insert_t>& inserts () const { return _inserts ; }
	const std::vector<volume_t>& volumes () const { return _volumes ; }
	const std::vector<route_t>&  routes () const { return _routes ; }
	const std::vector<strip_t>&  strips () const { return _strips ; }
	const std::vector<video_t>&  videofiles () const { return _videofiles ; }
	const std::vector<fade_t>&   fades () const { return _fades ; }

	const unsigned char* unxored_data () const { return _ptfunxored; }
	uint64_t             unxored_size () const { return _len; }

private:

	std::vector<wav_t>    _audiofiles;
	std::vector<region_t> _regions;
	std::vector<region_t> _midiregions;
	std::vector<track_t>  _tracks;
	std::vector<track_t>  _miditracks;
	std::vector<marker_t> _markers;
	std::vector<plugin_t> _plugins;
	std::vector<insert_t> _inserts;
	std::vector<volume_t> _volumes;
	std::vector<route_t>  _routes;
	std::vector<strip_t>  _strips;
	std::vector<video_t>  _videofiles;
	std::vector<fade_t>   _fades;
	std::vector<float>    _gains;

	std::string _path;
	std::vector<std::string> _sessionpath;
	std::vector<std::string> _sourcepath;

	unsigned char* _ptfunxored;
	uint64_t       _len;
	int64_t        _sessionrate;
	int            _sessionbitdepth;
	bool           _sessionfloat;
	uint32_t       _fpsnum;
	uint32_t       _fpsden;
	uint8_t        _version;
	uint8_t*       _product;
	int64_t        _targetrate;
	/* double: positions past 2^24 samples do not fit a float's mantissa. */
	double         _ratefactor;
	bool           is_bigendian;

	struct block_t {
		uint8_t zmark;			// 'Z'
		uint16_t block_type;		// type of block
		uint32_t block_size;		// size of block
		uint16_t content_type;		// type of content
		uint32_t offset;		// offset in file
		std::vector<block_t> child;	// vector of child blocks
	};
	std::vector<block_t> blocks;

	bool jumpback(uint32_t *currpos, unsigned char *buf, const uint32_t maxoffset, const unsigned char *needle, const uint32_t needlelen);
	bool jumpto(uint32_t *currpos, unsigned char *buf, const uint32_t maxoffset, const unsigned char *needle, const uint32_t needlelen);
	bool foundin(std::string const& haystack, std::string const& needle);
	int64_t foundat(unsigned char *haystack, uint64_t n, const char *needle);

	std::string parsestring(uint32_t pos);
	const std::string get_content_description(uint16_t ctype);
	int parse(void);
	void parseblocks(void);
	bool parseheader(void);
	void parsepaths(void);
	void parsemarkers(void);
	void parseplugins(void);
	void parsevolumes(void);
	void parsegains(void);
	void parsevideo(void);
	bool parsefade(uint32_t index, fade_t& fade);
	bool readstring(uint32_t& pos, uint32_t end, std::string& out);
	bool parserest(void);
	bool parseaudio(void);
	bool parsemidi(void);
	void dump(void);
	bool parse_block_at(uint32_t pos, struct block_t *b, struct block_t *parent, int level);
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
