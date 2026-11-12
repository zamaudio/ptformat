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
#include <map>
#include <set>
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

	/* Write the loaded session, with the changes made by the set_*
	 * calls, to path: encrypted again the way it was read, through a
	 * temporary file next to path that is then renamed. Without changes
	 * the file written is identical to the one loaded. Refuses (-1) to
	 * write over the file loaded; also -1 on a write error. 0 on
	 * success. */
	int save(std::string const& path) const;

	struct wav_t {
		std::string filename;
		uint16_t    index;

		int64_t     posabsolute;
		int64_t     length;
		/* When the file was created, in seconds since 1970-01-01 UTC;
		 * 0 when the session does not hold a valid date for it (files
		 * imported rather than recorded often have none). */
		int64_t     created;
		/* The file's 8-byte id, as 16 hex digits, from its 0x1001 block
		 * (PT10 on); empty when unknown. Pro Tools writes the same 8
		 * bytes into the WAV file, at bytes 7 to 14 of its "umid" chunk
		 * (all 1,048 files on disk checked, RF64 ones included), which
		 * tells the file apart from another with the same name. */
		std::string uid;

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
		/* Whether this placement of the region on a track is muted;
		 * only set on the regions of tracks(). */
		bool        muted;
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
		region_t (uint16_t idx = 0) : index (idx), startpos (0), sampleoffset (0), length (0), channel (0), gain (0), muted (false) {}
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
	 * strip stores none or the track has no strip (video tracks).
	 *
	 * format is the track's channel format code: 0 mono, 1 stereo, 6 for
	 * 5.1 and 14 for 7.1 in the sessions seen, where it matches the
	 * number of channels of the track; a video track holds 12.
	 *
	 * order is the track's position among the shown tracks, from 1, and 0
	 * for a hidden track (video tracks hold 0 too), as described in the
	 * format notes of the pt_api project. */
	/* A group of tracks, from the 0x202b block. tracks holds the names
	 * of its member tracks, in the order the group stores them. The
	 * built-in <ALL> group lists every track. Membership is read from
	 * the session's own track numbering (the full track list, video
	 * tracks included); not checked against Pro Tools. */
	struct group_t {
		std::string name;
		std::vector<std::string> tracks;
	};

	struct strip_t {
		std::string name;
		uint16_t    type;
		uint16_t    format;
		uint16_t    order;
		std::string id;
		std::string playlist;
		/* The name of the input path or bus the track's mixer strip
		 * stores; empty when no input is assigned or the track has no
		 * strip. In the sessions seen it matches an input path or bus
		 * of the I/O setup with the same id, except for paths missing
		 * from the session's I/O setup. Not checked in Pro Tools. */
		std::string input;
		strip_t () : type (0), format (0), order (0) {}
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
	 * stored with it; it equals the fixed value of the slot's mute lane
	 * (see automation_t), on outputs and on sends. pan and pan_right run from -100
	 * (left) to 100 (right); a stereo strip holds -100 and 100, a mono
	 * strip 0 in both. These four are deduced from the values, not
	 * checked in Pro Tools.
	 *
	 * block is where the slot is in the session (the offset of its
	 * 0x260d block), for set_level() and set_route_muted(); 0 when
	 * unknown. Routes of the same slot share it. */
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
		uint32_t    block;
		route_t () : strip_index (0), strip_type (0), slot (0), level (0), muted (false), pan (0), pan_right (0), block (0) {}
	};

	/* A volume the session uses: its name and where it was mounted (for
	 * example "Media" and "/Volumes/Media/"). */
	struct volume_t {
		std::string name;
		std::string path;
	};

	/* An automation lane of a mixer slot (see route_t for strip and
	 * slot). lane is its position among the lanes of the slot. Lane 0 of
	 * slot 0 is the track's volume, with values in dB. Lane 1 is the
	 * mute of the slot, 0 or 1: with a single point it equals
	 * route_t::muted in every slot of the sessions seen. The meaning of
	 * lanes 2 and 3 is not known. Each point is a position in samples
	 * at the target rate and a value (the stored integer divided by 10,
	 * or as stored for lane 1).
	 * A lane with a single point at 0 holds a fixed value. pan is 0 for
	 * those lanes; 1 (left, or the only one of a mono strip) or 2 (right)
	 * marks the pan position of the slot instead, with values from -100
	 * (left) to 100 (right) and lane 0. The layout comes from the format
	 * notes of the pt_api project, and every lane of the sessions seen
	 * matches it. */
	struct autopoint_t {
		int64_t pos;
		float   value;
		autopoint_t () : pos (0), value (0) {}
	};

	struct automation_t {
		std::string strip;
		uint16_t    strip_index;
		uint16_t    strip_type;
		uint16_t    slot;
		uint16_t    lane;
		uint8_t     pan;
		std::vector<autopoint_t> points;
		automation_t () : strip_index (0), strip_type (0), slot (0), lane (0), pan (0) {}
	};

	/* A tempo event: its position in ticks from the start of the
	 * timeline (960000 per quarter note) and the tempo in beats per
	 * minute. Every session seen holds a single event at 0, so reading
	 * more than one is not verified. */
	struct tempo_t {
		int64_t pos;
		double  bpm;
		tempo_t () : pos (0), bpm (0) {}
	};

	/* A path of the session's I/O setup: its name, its number of
	 * channels, and for a sub-path (for example "Bus 1.L") the name of
	 * the path it belongs to. list is the block it comes from: 0x2603
	 * holds the paths that tracks send to (outputs and buses), 0x1022
	 * the ones in the sessions seen that look like inputs. id is the
	 * 8-byte id routes refer to, as 16 hex digits. */
	struct iopath_t {
		std::string name;
		uint32_t    channels;
		std::string parent;
		uint16_t    list;
		std::string id;
		iopath_t () : channels (0), list (0) {}
	};

	/* A fade on an audio track. type is 3 for a fade-in (it starts where
	 * a region starts), 2 for a fade-out (it ends where a region ends)
	 * and 1 for a crossfade (across the cut where one region ends and the
	 * next starts). start and length are in samples at the target rate.
	 * Before this was corrected, start held the end of a fade-out and the
	 * cut of a crossfade. For a
	 * crossfade, split is the part of length before the cut; otherwise it
	 * equals length. shape is a second byte stored with the type: 1 equal
	 * power and 2 linear according to the pt_api format notes. trackindex is the position of the track in
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

	/* entry is where the placement is stored in the decrypted session
	 * (the offset of its 0x104f block), for set_muted(); 0 when unknown. */
	struct track_t {
		std::string name;
		uint16_t    index;
		uint8_t     playlist;
		region_t    reg;
		uint32_t    entry;

		bool operator <(const track_t& other) const {
			return (this->index < other.index);
		}

		bool operator ==(const track_t& other) const {
			return (this->index == other.index);
		}
		track_t (uint16_t idx = 0) : index (idx), playlist (0), entry (0) {}
	};

	/* Mute or unmute one placement of tracks() in the loaded session:
	 * sets the first byte of its 0x104f entry, the static mute per the
	 * pt_api format notes (section 4.2), and updates tracks(). Changes
	 * nothing on disk until save(). False when the placement does not
	 * come from this session. */
	bool set_muted(const track_t& placement, bool muted);

	/* Set the level, in dB from -80 to +12 (stored in tenths), or the
	 * mute of the mixer slot of a route of routes(): the track fader or
	 * output mute for slot 0, a send for the others. Both the value in
	 * the slot's 0x1029 and the single point of its level or mute lane
	 * change, as they hold the same value in every session seen; routes()
	 * and automation() are updated. False when the route does not come
	 * from this session, the level is out of range, or the lane holds
	 * automation (more than one point). Changes nothing on disk until
	 * save(). Not checked in Pro Tools. */
	bool set_level(const route_t& route, float db);
	bool set_route_muted(const route_t& route, bool muted);

	/* Send a route of routes() to another output path or bus of the
	 * session: the path id, the name and the two flag bytes of its
	 * destination record (0x260e) are replaced by those of another
	 * record that already sends to destination, and so is the copy of
	 * the record under the strip's 0x4856 when the strip has one. The
	 * route's own source byte is kept. The destination must be an output
	 * path (0x2603 list) with the same number of channels as the current
	 * one, and some strip of the session must already send to it (its
	 * record is the model). False, with nothing changed, otherwise, or
	 * when the offsets cannot be moved without doubt. routes() is
	 * updated; earlier copies of route are then stale. Changes nothing
	 * on disk until save(). Not checked in Pro Tools. */
	bool set_route_destination(const route_t& route, const std::string& destination);

	/* Set the input of the track of strips() with this id to the input
	 * path or bus named input (a main path of the 0x1022 list), or to no
	 * input when input is empty. The track's mixer strip stores the
	 * input's name, the path's id and a byte that tells a bus from a
	 * hardware input, in its 0x261b; no other place of the session holds
	 * them. The path must have as many channels as the track (1 for a
	 * mono track, 2 for a stereo one; other formats are refused). False,
	 * with nothing changed, when there is no such track or strip, the
	 * input is already that one, no single path has the name, the
	 * channels differ, the record is not as expected, or the offsets
	 * cannot be moved without doubt. strips() is updated. Changes nothing
	 * on disk until save(). Not checked in Pro Tools. */
	bool set_input(const std::string& id, const std::string& input);

	/* Move one placement of tracks() to pos, in samples at the target
	 * rate, from 00:00:00:00: the 8-byte position of its 0x104f entry
	 * (pt_api format notes, sections 4.2 and 6.1) changes and the audio
	 * entries of the playlist are put back in time order. Each channel
	 * of a multichannel track is a placement of its own. False, with
	 * nothing changed, when the placement would overlap another region
	 * of its playlist, when a fade touches it or lies between the old
	 * and new places, or for a big-endian session. The session is then
	 * parsed again. Changes nothing on disk until save(). */
	bool set_position(const track_t& placement, int64_t pos);

	/* Split one placement of tracks() at cut, in samples from
	 * 00:00:00:00: two regions are added for the parts before and after
	 * the cut, named like Pro Tools names them (the region's name, -01,
	 * -02, after the highest number in use), with the layout of the
	 * sub-regions Pro Tools makes in the sessions seen. The placement then
	 * plays the first part and a new placement right after it the
	 * second. Each channel of a multichannel track is a placement of its
	 * own. Only when the session was loaded at its own rate; false, with
	 * nothing changed, when the cut is not inside the placement, a fade
	 * touches it, or the region or session has a layout not seen. The
	 * session is then parsed again. Changes nothing on disk until save(). */
	bool split_region(const track_t& placement, int64_t cut);

	/* Add a fade-in at the start or a fade-out at the end of one
	 * placement of tracks(), length samples long, with shape 1 (equal
	 * power) or 2 (linear). A record is added to the fade list (0x2630)
	 * and an entry to the playlist, both as Pro Tools stores them in the
	 * sessions seen. Each channel of a multichannel track is a placement
	 * of its own. Only when the session was loaded at its own rate and
	 * already holds a fade to copy the record header from; false, with
	 * nothing changed, when the fade is longer than the region, a fade is
	 * already on that edge, or another region touches the placement. The
	 * session is then parsed again. Changes nothing on disk until save(). */
	bool add_fade(const track_t& placement, bool fade_in, int64_t length, uint8_t shape);

	/* Rename a region of regions() in the loaded session; every
	 * placement of it shows the new name. The name is stored in the
	 * region's 0x2628 block, so that block and the two holding it change
	 * size, and every later block moves: the offsets that point at them
	 * (block 0x0001 and the final 0x0002 block) are moved too, following
	 * the pt_api format notes (section 2) with a stricter rule for the
	 * pointers outside the standard records (see the code). The session is then
	 * parsed again, so regions(), tracks() and the rest follow. Only for
	 * little-endian PT10 to PT12 sessions. False, with nothing changed,
	 * when the region is not found, another region already has the name,
	 * the name is empty or holds a NUL, or the offsets cannot be moved
	 * without doubt. Changes nothing on disk until save(). */
	bool rename_region(const region_t& region, const std::string& name);

	/* Add a marker named name at pos (samples at the session rate) to
	 * the memory locations (0x2030): a copy of the session's first
	 * marker block with the next free number, the new name and position
	 * and a new UUID, placed in number order, and the location count
	 * raised by one. The rest of the copy (window and track properties
	 * of the location) is the template's. False, with nothing changed,
	 * when the session holds no marker to copy, the name is empty or
	 * holds a NUL, the target rate differs from the session rate, or
	 * the offsets cannot be moved without doubt. Changes nothing on disk
	 * until save(). Not checked in Pro Tools. */
	bool add_marker(const std::string& name, int64_t pos);

	/* Rename the track of strips() whose id is id. The name is stored,
	 * with its length first, in the record list at the start of the
	 * 0x2519 track list, in both copies of its 0x251a, in its 0x210b of
	 * the 0x2107 list, in its 0x1014 of the 0x1015 list for an audio
	 * track, and in the 0x2619 of its mixer strip when the strip holds
	 * the track name (aux and master strips; an audio track's strip
	 * holds its playlist name, which is not changed). Each is found by
	 * the track's id. False, with nothing changed, when no track has
	 * the id, another track already has the name, the name is empty or
	 * holds a NUL, a place does not hold the name as expected, or the
	 * offsets cannot be moved without doubt. Changes nothing on disk
	 * until save(). Not checked in Pro Tools. */
	bool rename_track(const std::string& id, const std::string& name);

	/* Rename the I/O Setup path of iopaths() with this list and id (the
	 * output or bus path of the 0x2603 list, or an input path of the
	 * 0x1022 list); its sub-paths keep their names. The name is stored,
	 * with its length first, in the path's own block (three times in an
	 * input list entry), in the channel records under it (0x2600), in the
	 * destination record (0x260e) of every send and output that goes to
	 * the path and its copy under 0x4856, in the input record of the
	 * strips that record from it, and, for a hardware output, in the
	 * name table 0x2033. The path in the other list that holds the same
	 * id is renamed too. False, with nothing changed, when no path has
	 * the id, another path has the name, the name is empty or holds a NUL,
	 * a place does not hold the name as expected, or the offsets cannot
	 * be moved without doubt. iopaths(), routes() and strips() are
	 * updated. Changes nothing on disk until save(). Not checked in Pro
	 * Tools. */
	bool rename_iopath(const std::string& id, const std::string& name);

	/* Set the clip gain of a region of regions() in the loaded session,
	 * in dB (-HUGE_VALF for -inf); every placement of it follows. When
	 * no other region shares the region's gain point, its value changes
	 * in place; otherwise, or when the region has none, a point is added
	 * to the 0x2637 list for it, as the pt_api format notes describe
	 * (section 9). The session is then parsed again. Only for single
	 * value points, little-endian PT10 to PT12 sessions and sessions
	 * without clip groups; false, with nothing changed, otherwise or for
	 * a NaN or +inf value. Changes nothing on disk until save(). */
	bool set_clip_gain(const region_t& region, float db);

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
	/* The session's timecode rate as a fraction (for example
	 * 24000/1001), and whether it counts drop frame. It comes from the
	 * frame rate code of the 0x204d block, or, when that is absent or
	 * unknown, from the frame rate of the video format (never drop
	 * frame). The denominator is 0 when the session holds neither. Track
	 * positions are samples counted from timecode 00:00:00:00. */
	uint32_t frameratenum () const { return _fpsnum ; }
	uint32_t framerateden () const { return _fpsden ; }
	bool dropframe () const { return _dropframe ; }
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
	const std::vector<group_t>&  groups () const { return _groups ; }
	const std::vector<video_t>&  videofiles () const { return _videofiles ; }
	const std::vector<fade_t>&   fades () const { return _fades ; }
	const std::vector<iopath_t>& iopaths () const { return _iopaths ; }
	const std::vector<tempo_t>&  tempos () const { return _tempos ; }
	const std::vector<automation_t>& automation () const { return _automation ; }

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
	std::vector<group_t>  _groups;
	std::vector<video_t>  _videofiles;
	std::vector<fade_t>   _fades;
	std::vector<iopath_t> _iopaths;
	std::vector<tempo_t>  _tempos;
	std::vector<automation_t> _automation;
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
	bool           _dropframe;
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
	void parsegroups(void);
	bool input_record(const block_t& strip, uint32_t& at, uint32_t& end) const;
	void read_input(const block_t& strip, std::string& input);
	bool set_slot(const route_t& route, uint16_t lane, int32_t value);
	struct name_field {
		uint32_t at;	/* the 4-byte length before the name */
		std::vector<const block_t*> grow;
	};
	bool track_name_fields(const std::string& id, const std::string& name, std::vector<name_field>& out) const;
	bool iopath_name_fields(const std::string& id, const std::string& old, std::vector<std::pair<uint32_t, uint32_t> >& out) const;
	bool dest_payload(const block_t& z, std::string& payload, std::string& name) const;
	bool chain_to(const block_t& b, uint32_t offset, std::vector<const block_t*>& chain) const;
	void parsevolumes(void);
	void parsegains(void);
	void parsevideo(void);
	void parseio(void);
	void parsetempo(void);
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
	uint8_t gen_xor_delta(uint8_t xor_value, uint8_t mul, bool negative) const;
	/* A 0x1050 entry of a playlist: where it is stored, where its 0x104f
	 * is, whether it is a fade, and the time it covers. */
	struct playlist_slot {
		uint32_t start, end, entry;
		bool fade;
		int64_t from, to;
	};
	bool playlist_of(uint32_t e, const block_t*& list, std::vector<playlist_slot>& slots, size_t& self);
	bool reparse(void);
	std::string region_part(const block_t& def, const block_t& info, const std::string& name,
		uint64_t offset, uint64_t length, const std::string& id) const;
	bool region_blocks(uint16_t index, const block_t*& list, const block_t*& def, const block_t*& info) const;
	uint32_t gain_field(const block_t& info) const;
	bool splice(uint32_t at, uint32_t oldlen, const std::string& data,
		const std::vector<const block_t*>& grow,
		const std::vector<std::pair<uint32_t, uint32_t> >& set);
	static void collect_headers(const std::vector<block_t>& v, std::map<uint32_t, const block_t*>& out);
	void setrates(void);
	void cleanup(void);
	void free_block(struct block_t& b);
	void free_all_blocks(void);
};

#endif
