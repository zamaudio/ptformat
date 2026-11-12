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

/*
 * ptbounce: write the audio of one track of a session to a WAV file,
 * with the clip gain of each region and the track's volume applied.
 *
 * Usage: ptbounce [--from at] [--to at] session.pt{x,f} track out.wav
 *                 [audio dir ...]
 *
 * The audio files are looked up by name in the audio dirs given, or in
 * "<session dir>/Audio Files". The WAV written is 32-bit float at the
 * session's rate, with one channel per track channel, from the start of
 * the track's first region to the end of its last one, or from --from
 * to --to (timecode hh:mm:ss:ff counted from 00:00:00:00, or samples at
 * the session's rate); its bext chunk
 * holds that start (samples from 00:00:00:00), so it lines up when
 * imported with its time stamp.
 *
 * Each sample gets the clip gain of its region plus the track volume at
 * that moment, added in dB: the volume automation of the track's output
 * (lane 0 of slot 0) when there is any, else its fader. Between two
 * automation points the volume is taken as a straight line in dB; how
 * Pro Tools draws it is not checked yet. Muted regions are silent.
 *
 * Fade-ins, fade-outs and crossfades are applied: linear ones as a
 * straight line in amplitude, equal power ones as a quarter sine. Across
 * a crossfade each region plays on from its file past its edge and the
 * two add up. The other fade curves and slopes Pro Tools offers are not
 * read, so a fade with one of them is drawn as one of these two.
 *
 * Not applied: plugins (clip gain comes before them and the volume after,
 * so this is exact only without plugins or with linear ones), clip gain
 * lines with more than one point (the library reads one gain
 * per region), trim, VCA, mute, pan, sends and anything after the track
 * output.
 *
 * The session and the audio files are only read; out.wav must not exist.
 *
 * Exit status: 0 success, 1 an audio file is missing or unreadable (the
 * WAV is still written, with silence in its place), 2 usage, read or
 * write error.
 */

#include "ptformat/ptformat.h"
#include <inttypes.h>
#include <sys/stat.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace std;

static uint32_t
le32(const unsigned char *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t
le64(const unsigned char *p)
{
	return le32(p) | ((uint64_t)le32(p + 4) << 32);
}

/* A WAV (RIFF or RF64) file open for reading: integer PCM of 8 to 32
 * bits or 32/64-bit float. */
struct wavfile {
	FILE *f;
	uint16_t channels;
	uint16_t bits;
	bool is_float;
	uint64_t data;		/* offset of the first sample */
	uint64_t frames;
	wavfile () : f (NULL), channels (0), bits (0), is_float (false), data (0), frames (0) {}
};

static bool
open_wav(const string& path, wavfile& w)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	unsigned char h[12];
	if (fread(h, 1, 12, f) != 12 || (memcmp(h, "RIFF", 4) && memcmp(h, "RF64", 4)) || memcmp(h + 8, "WAVE", 4)) {
		fclose(f);
		return false;
	}
	uint64_t ds64 = 0, pos = 12;
	bool fmt = false;
	for (;;) {
		unsigned char c[8];
		if (fseeko(f, (off_t)pos, SEEK_SET) || fread(c, 1, 8, f) != 8)
			break;
		uint64_t size = le32(c + 4);
		if (!memcmp(c, "ds64", 4) && size >= 16) {
			unsigned char d[16];
			if (fread(d, 1, 16, f) != 16)
				break;
			ds64 = le64(d + 8);
		} else if (!memcmp(c, "fmt ", 4) && size >= 16) {
			unsigned char d[40];
			size_t n = size < 40 ? size : 40;
			if (fread(d, 1, n, f) != n)
				break;
			uint16_t tag = d[0] | (d[1] << 8);
			w.channels = d[2] | (d[3] << 8);
			w.bits = d[14] | (d[15] << 8);
			if (tag == 0xfffe && n >= 26)
				tag = d[24] | (d[25] << 8);	/* subformat */
			if (tag != 1 && tag != 3)
				break;
			w.is_float = (tag == 3);
			fmt = true;
		} else if (!memcmp(c, "data", 4)) {
			if (size == 0xffffffff)
				size = ds64;
			w.data = pos + 8;
			uint32_t bytes = w.bits / 8 * w.channels;
			if (fmt && bytes) {
				w.frames = size / bytes;
				w.f = f;
				if ((w.is_float && w.bits != 32 && w.bits != 64) ||
						(!w.is_float && (w.bits < 8 || w.bits > 32 || w.bits % 8)))
					break;
				return true;
			}
			break;
		}
		pos += 8 + size + (size & 1);
	}
	fclose(f);
	return false;
}

/* Read frames [from, from + n) of channel ch into out as floats; frames
 * past the end of the file read as 0. */
static bool
read_channel(wavfile& w, uint16_t ch, uint64_t from, size_t n, vector<float>& out)
{
	out.assign(n, 0.f);
	if (from >= w.frames)
		return true;
	if (from + n > w.frames)
		n = (size_t)(w.frames - from);
	uint32_t bps = w.bits / 8, frame = bps * w.channels;
	vector<unsigned char> buf((size_t)n * frame);
	if (fseeko(w.f, (off_t)(w.data + from * frame), SEEK_SET) || fread(&buf[0], 1, buf.size(), w.f) != buf.size())
		return false;
	for (size_t i = 0; i < n; i++) {
		const unsigned char *p = &buf[i * frame + ch * bps];
		float v;
		if (w.is_float && bps == 4) {
			uint32_t u = le32(p);
			memcpy(&v, &u, 4);
		} else if (w.is_float) {
			uint64_t u = le64(p);
			double d;
			memcpy(&d, &u, 8);
			v = (float)d;
		} else if (bps == 1) {
			v = (p[0] - 128) / 128.f;
		} else {
			int32_t s = 0;
			for (uint32_t k = 0; k < bps; k++)
				s |= (int32_t)p[k] << (8 * (k + 4 - bps));
			v = (float)(s / 2147483648.0);
		}
		out[i] = v;
	}
	return true;
}

/* The track volume in dB at position t: a straight line in dB between
 * two points, the first value before the first point and the last after
 * the last one. */
static double
volume_at(const vector<PTFFormat::autopoint_t>& p, int64_t t)
{
	if (t <= p.front().pos)
		return p.front().value;
	if (t >= p.back().pos)
		return p.back().value;
	size_t hi = 1;
	while (p[hi].pos < t)
		hi++;
	const PTFFormat::autopoint_t& a = p[hi - 1];
	const PTFFormat::autopoint_t& b = p[hi];
	if (b.pos == a.pos)
		return b.value;
	return a.value + (b.value - a.value) * (double)(t - a.pos) / (double)(b.pos - a.pos);
}

/* A fade over [start, start + length). shape 1 is equal power (a
 * quarter sine) and anything else a straight line in amplitude, after
 * the pt_api format notes; neither is checked against Pro Tools. */
struct fade {
	int64_t start;
	int64_t length;
	uint8_t shape;
	fade () : start (0), length (0), shape (0) {}

	/* The gain at s: rising for a fade-in, falling for a fade-out;
	 * 1 outside the fade. */
	double gain(int64_t s, bool rising) const {
		if (length <= 0 || s < start || s >= start + length)
			return 1.0;
		double x = (s - start + 0.5) / (double)length;
		if (!rising)
			x = 1.0 - x;
		return shape == 1 ? sin(x * M_PI / 2) : x;
	}
};

/* A placement with the fades that touch it, and the samples it covers:
 * past its own edges where a crossfade reaches. */
struct span {
	PTFFormat::track_t t;
	int64_t from;
	int64_t to;
	fade in;
	fade out;
	span () : from (0), to (0) {}
};

static bool
put32(FILE *f, uint32_t v)
{
	unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
	return fwrite(b, 1, 4, f) == 4;
}

static bool
put16(FILE *f, uint16_t v)
{
	unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) };
	return fwrite(b, 1, 2, f) == 2;
}

static const uint32_t BEXT_SIZE = 602;

/* Header of a 32-bit float WAV with a bext chunk holding the time
 * reference; data_bytes is filled in at the end. */
static bool
write_header(FILE *f, uint16_t channels, uint32_t rate, int64_t start, uint32_t data_bytes)
{
	bool ok = fwrite("RIFF", 1, 4, f) == 4 && put32(f, 4 + 8 + BEXT_SIZE + 8 + 18 + 8 + data_bytes) &&
		fwrite("WAVE", 1, 4, f) == 4 &&
		fwrite("bext", 1, 4, f) == 4 && put32(f, BEXT_SIZE);
	unsigned char bext[BEXT_SIZE];
	memset(bext, 0, sizeof bext);
	snprintf((char *)bext + 256, 32, "ptbounce");
	uint64_t ref = (uint64_t)start;
	for (int k = 0; k < 8; k++)
		bext[338 + k] = (unsigned char)(ref >> (8 * k));
	bext[346] = 1;	/* version */
	ok = ok && fwrite(bext, 1, sizeof bext, f) == sizeof bext &&
		fwrite("fmt ", 1, 4, f) == 4 && put32(f, 18) &&
		put16(f, 3) && put16(f, channels) && put32(f, rate) &&
		put32(f, rate * 4 * channels) && put16(f, (uint16_t)(4 * channels)) &&
		put16(f, 32) && put16(f, 0) &&
		fwrite("data", 1, 4, f) == 4 && put32(f, data_bytes);
	return ok;
}

static string
session_dir(const string& path)
{
	size_t slash = path.find_last_of('/');
	if (slash == string::npos)
		return ".";
	if (slash == 0)
		return "/";
	return path.substr(0, slash);
}

/* Samples at rate for a timecode or a sample count; -1 when neither. */
static int64_t
position(const char *s, int64_t rate, uint32_t num, uint32_t den, bool drop)
{
	unsigned h, m, sec, f;
	char sep;
	if (sscanf(s, "%u:%u:%u%c%u", &h, &m, &sec, &sep, &f) == 5 && (sep == ':' || sep == ';')) {
		if (!num || !den || m > 59 || sec > 59)
			return -1;
		int64_t base = (num + den / 2) / den;
		if ((int64_t)f >= base)
			return -1;
		int64_t minutes = 60 * (int64_t)h + m;
		int64_t frames = (60 * minutes + sec) * base + f;
		if (drop) {
			int64_t dropped = 2 * (base / 30);
			frames -= dropped * (minutes - minutes / 10);
		}
		/* Round to the nearest sample. */
		return (2 * frames * rate * den + num) / (2 * (int64_t)num);
	}
	char *end;
	long long v = strtoll(s, &end, 10);
	if (*end || end == s || v < 0)
		return -1;
	return v;
}

int main (int argc, char **argv) {
	const char *range[2] = { NULL, NULL };
	int arg = 1;
	while (arg + 1 < argc && (!strcmp(argv[arg], "--from") || !strcmp(argv[arg], "--to"))) {
		range[argv[arg][2] == 't'] = argv[arg + 1];
		arg += 2;
	}
	if (argc - arg < 3) {
		fprintf(stderr, "Usage: %s [--from at] [--to at] session.pt{x,f} track out.wav [audio dir ...]\n", argv[0]);
		return 2;
	}
	const char *in = argv[arg];
	string track = argv[arg + 1];
	const char *out = argv[arg + 2];
	vector<string> dirs;
	for (int i = arg + 3; i < argc; i++)
		dirs.push_back(argv[i]);
	if (dirs.empty())
		dirs.push_back(session_dir(in) + "/Audio Files");

	struct stat st;
	if (stat(out, &st) == 0) {
		fprintf(stderr, "%s already exists\n", out);
		return 2;
	}

	/* Read once to learn the session's rate, then at that rate, so
	 * positions are the session's own samples. */
	PTFFormat probe;
	if (probe.load(in, 48000) != 0) {
		fprintf(stderr, "Cannot read session %s\n", in);
		return 2;
	}
	int64_t rate = probe.sessionrate();
	PTFFormat ptf;
	int ok = ptf.load(in, rate);
	if (ok != 0) {
		fprintf(stderr, "Cannot read session %s (error %d)\n", in, ok);
		return 2;
	}

	/* The track's channels, in the order the track list stores them,
	 * and its placements. */
	vector<uint16_t> chans;
	vector<PTFFormat::track_t> placed;
	for (vector<PTFFormat::track_t>::const_iterator t = ptf.tracks().begin(); t != ptf.tracks().end(); ++t) {
		if (t->name != track)
			continue;
		if (find(chans.begin(), chans.end(), t->index) == chans.end())
			chans.push_back(t->index);
		if (t->reg.index != 65535 && t->reg.length > 0)
			placed.push_back(*t);
	}
	if (chans.empty()) {
		fprintf(stderr, "No audio track named %s\n", track.c_str());
		return 2;
	}
	if (placed.empty()) {
		fprintf(stderr, "Track %s has no regions\n", track.c_str());
		return 2;
	}

	/* The volume: automation of lane 0 of the output, else the fader. */
	vector<PTFFormat::autopoint_t> volume;
	for (vector<PTFFormat::automation_t>::const_iterator a = ptf.automation().begin(); a != ptf.automation().end(); ++a) {
		if (a->strip == track && a->slot == 0 && a->lane == 0 && a->pan == 0 && !a->points.empty()) {
			volume = a->points;
			break;
		}
	}
	if (volume.empty()) {
		PTFFormat::autopoint_t p;
		for (vector<PTFFormat::route_t>::const_iterator r = ptf.routes().begin(); r != ptf.routes().end(); ++r) {
			if (r->strip == track && r->slot == 0) {
				p.value = r->level;
				break;
			}
		}
		volume.push_back(p);
	}

	int64_t start = placed[0].reg.startpos, end = start;
	for (size_t i = 0; i < placed.size(); i++) {
		start = min(start, placed[i].reg.startpos);
		end = max(end, placed[i].reg.startpos + placed[i].reg.length);
	}
	for (int k = 0; k < 2; k++) {
		if (!range[k])
			continue;
		int64_t at = position(range[k], rate, ptf.frameratenum(), ptf.framerateden(), ptf.dropframe());
		if (at < 0) {
			fprintf(stderr, "Not a timecode or a sample count: %s\n", range[k]);
			return 2;
		}
		(k ? end : start) = at;
	}
	if (end <= start) {
		fprintf(stderr, "Nothing to write: the end is not after the start\n");
		return 2;
	}
	uint16_t nch = (uint16_t)chans.size();
	uint64_t total = (uint64_t)(end - start);
	if (total * 4 * nch > 0xffffffffULL - 1024) {
		fprintf(stderr, "Track %s is too long for a WAV file\n", track.c_str());
		return 2;
	}

	/* The fades of each placement, matched by position: a fade-in
	 * starts where a region starts, a fade-out ends where one ends,
	 * and a crossfade's cut is where one ends and the next starts.
	 * Fades are matched on every channel of the track, as Pro Tools
	 * fades all channels together. Across a crossfade each region
	 * plays on from its file past its own edge. */
	vector<span> spans;
	for (size_t i = 0; i < placed.size(); i++) {
		span sp;
		sp.t = placed[i];
		int64_t rs = sp.t.reg.startpos, re = rs + sp.t.reg.length;
		sp.from = rs;
		sp.to = re;
		for (vector<PTFFormat::fade_t>::const_iterator fd = ptf.fades().begin(); fd != ptf.fades().end(); ++fd) {
			if (fd->track != track || fd->length <= 0)
				continue;
			int64_t fs = fd->start, fe = fd->start + fd->length, cut = fd->start + fd->split;
			fade ramp;
			ramp.start = fs;
			ramp.length = fd->length;
			ramp.shape = fd->shape;
			if ((fd->type == 3 && fs == rs) || (fd->type == 1 && cut == rs)) {
				sp.in = ramp;
				sp.from = min(sp.from, fs);
			}
			if ((fd->type == 2 && fe == re) || (fd->type == 1 && cut == re)) {
				sp.out = ramp;
				sp.to = max(sp.to, fe);
			}
		}
		spans.push_back(sp);
	}

	/* Index the audio files on disk by name. */
	map<string, string> files;
	for (size_t d = 0; d < dirs.size(); d++) {
		for (size_t i = 0; i < placed.size(); i++) {
			const string& name = placed[i].reg.wave.filename;
			if (files.count(name))
				continue;
			string path = dirs[d] + "/" + name;
			if (stat(path.c_str(), &st) == 0)
				files[name] = path;
		}
	}

	FILE *f = fopen(out, "wbx");
	if (!f) {
		fprintf(stderr, "Cannot create %s\n", out);
		return 2;
	}
	uint32_t data_bytes = (uint32_t)(total * 4 * nch);
	if (!write_header(f, nch, (uint32_t)rate, start, data_bytes)) {
		fprintf(stderr, "Cannot write %s\n", out);
		fclose(f);
		return 2;
	}

	map<string, wavfile> open;
	vector<string> missing;
	const size_t block = 65536;
	vector<float> mix, src;
	int status = 0;
	for (int64_t b = start; b < end; b += block) {
		size_t n = (size_t)min((int64_t)block, end - b);
		mix.assign(n * nch, 0.f);
		/* Each channel: the regions placed on it, with their fades;
		 * across a crossfade the two regions add up. */
		for (size_t i = 0; i < spans.size(); i++) {
			const span& sp = spans[i];
			const PTFFormat::track_t& t = sp.t;
			int64_t rs = t.reg.startpos;
			if (sp.to <= b || sp.from >= b + (int64_t)n || t.reg.muted)
				continue;
			uint16_t c = (uint16_t)(find(chans.begin(), chans.end(), t.index) - chans.begin());
			int64_t from = max(sp.from, b), to = min(sp.to, b + (int64_t)n);
			const string& name = t.reg.wave.filename;
			if (!open.count(name)) {
				wavfile w;
				if (!files.count(name) || !open_wav(files[name], w)) {
					if (find(missing.begin(), missing.end(), name) == missing.end()) {
						missing.push_back(name);
						fprintf(stderr, "Cannot read audio file %s\n", name.c_str());
					}
					status = 1;
				}
				open[name] = w;
			}
			wavfile& w = open[name];
			if (!w.f || t.reg.channel >= w.channels)
				continue;
			/* A crossfade can reach before the start of the file: that
			 * part is silent. */
			int64_t at = t.reg.sampleoffset + (from - rs);
			int64_t skip = at < 0 ? -at : 0;
			if (skip >= to - from)
				continue;
			if (!read_channel(w, (uint16_t)t.reg.channel, (uint64_t)(at + skip), (size_t)(to - from - skip), src)) {
				fprintf(stderr, "Cannot read audio file %s\n", name.c_str());
				status = 1;
				continue;
			}
			double clip = t.reg.gain;
			for (int64_t s = from + skip; s < to; s++)
				mix[(size_t)(s - b) * nch + c] += (float)(src[(size_t)(s - from - skip)] *
					sp.in.gain(s, true) * sp.out.gain(s, false) *
					pow(10.0, (clip + volume_at(volume, s)) / 20.0));
		}
		for (size_t k = 0; k < mix.size(); k++) {
			uint32_t u;
			memcpy(&u, &mix[k], 4);
			if (!put32(f, u)) {
				fprintf(stderr, "Cannot write %s\n", out);
				fclose(f);
				return 2;
			}
		}
	}
	for (map<string, wavfile>::iterator o = open.begin(); o != open.end(); ++o)
		if (o->second.f)
			fclose(o->second.f);
	if (fclose(f) != 0) {
		fprintf(stderr, "Cannot write %s\n", out);
		return 2;
	}
	printf("%s: %u channel%s, %" PRId64 " samples from sample %" PRId64 ", %zu region%s\n",
		track.c_str(), nch, nch == 1 ? "" : "s", (int64_t)total, start, placed.size(), placed.size() == 1 ? "" : "s");
	return status;
}
