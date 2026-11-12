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
 * pttracks: list the regions placed on each audio track, in time order.
 *
 * Usage: pttracks [--csv] file.pt{s,5,f,x} [track name ...]
 *
 * With track names, only those tracks are listed. Positions are shown
 * as timecode (hh:mm:ss:ff, counted from 00:00:00:00 at the session's
 * frame rate) when the session holds a frame rate, and as h:mm:ss.sss
 * from the start of the timeline when it does not; --csv gives samples,
 * clock time and timecode. Each track
 * shows the time covered by at least one of its regions, so channels of
 * a multichannel track that overlap count once. Each region shows when
 * its file was created (UTC), when the session holds that date. --csv
 * prints one row per region with a header line.
 *
 * Exit status: 0 success, 2 usage or read error or a track name not
 * found.
 */

#include "ptformat/ptformat.h"
#include <inttypes.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace std;

struct placement {
	int64_t start;
	string region;
	string file;
	int64_t offset;
	int64_t length;
	int64_t created;
	float gain;
	string recorded_on;

	bool operator <(const placement& o) const {
		if (start != o.start)
			return start < o.start;
		return region < o.region;
	}
};

static string
clock(int64_t samples, int64_t rate)
{
	char buf[32];
	double s = (double)samples / rate;
	int h = (int)(s / 3600);
	int m = (int)((s - h * 3600) / 60);
	snprintf(buf, sizeof buf, "%d:%02d:%06.3f", h, m, s - h * 3600 - m * 60);
	return buf;
}

/* Timecode hh:mm:ss:ff of a position counted from 00:00:00:00, at a
 * frame rate of num/den; "" when the rate is unknown. Frames are counted
 * non-drop at the rounded rate (24 for 24000/1001, 30 for 30000/1001). */
static string
timecode(int64_t samples, int64_t rate, uint32_t num, uint32_t den)
{
	if (!num || !den || samples < 0)
		return "";
	int64_t frames = samples * num / (rate * den);
	int64_t base = (num + den / 2) / den;
	if (base <= 0)
		return "";
	char buf[32];
	int64_t s = frames / base;
	snprintf(buf, sizeof buf, "%02d:%02d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60),
		(int)(s % 60), (int)(frames % base));
	return buf;
}

/* A creation time in UTC, or an empty string when unknown (0). */
static string
date(int64_t t, const char *format)
{
	if (!t)
		return "";
	char buf[32];
	time_t tt = (time_t)t;
	strftime(buf, sizeof buf, format, gmtime(&tt));
	return buf;
}

/* Quote a CSV field when it holds a comma, a quote or a newline. */
static string
csv(const string& s)
{
	if (s.find_first_of(",\"\n") == string::npos)
		return s;
	string out = "\"";
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '"')
			out += '"';
		out += s[i];
	}
	return out + "\"";
}

int main (int argc, char **argv) {
	bool as_csv = false;
	int arg = 1;
	if (arg < argc && strcmp(argv[arg], "--csv") == 0) {
		as_csv = true;
		arg++;
	}
	if (arg >= argc) {
		fprintf(stderr, "Usage: %s [--csv] file.pt{s,5,f,x} [track name ...]\n", argv[0]);
		return 2;
	}

	PTFFormat ptf;
	const char *path = argv[arg++];
	int ok = ptf.load(path, 48000);
	if (ok != 0) {
		fprintf(stderr, "Cannot read session %s (error %d)\n", path, ok);
		return 2;
	}
	/* The library scales positions to the rate given to load(). */
	int64_t rate = 48000;
	uint32_t fpsnum = ptf.frameratenum(), fpsden = ptf.framerateden();

	set<string> wanted;
	for (; arg < argc; arg++)
		wanted.insert(argv[arg]);

	/* Track name -> its placements, keeping the order tracks appear in. */
	vector<string> order;
	map<string, set<placement> > tracks;
	for (vector<PTFFormat::track_t>::const_iterator t = ptf.tracks().begin();
			t != ptf.tracks().end(); ++t) {
		if (!wanted.empty() && wanted.find(t->name) == wanted.end())
			continue;
		if (tracks.find(t->name) == tracks.end())
			order.push_back(t->name);
		set<placement>& s = tracks[t->name];
		/* Tracks with no region carry a dummy one with index 65535. */
		if (t->reg.index == 65535)
			continue;
		placement p;
		p.start = t->reg.startpos;
		p.region = t->reg.name;
		p.file = t->reg.wave.filename;
		p.offset = t->reg.sampleoffset;
		p.length = t->reg.length;
		p.created = t->reg.wave.created;
		p.gain = t->reg.gain;
		p.recorded_on = t->reg.recorded_on;
		s.insert(p);
	}

	bool missing = false;
	for (set<string>::const_iterator w = wanted.begin(); w != wanted.end(); ++w) {
		if (tracks.find(*w) == tracks.end()) {
			fprintf(stderr, "No track named %s\n", w->c_str());
			missing = true;
		}
	}

	if (as_csv)
		printf("track,region,file,start,start_time,end,end_time,length,file_offset,file_created,gain_db,start_tc,end_tc,recorded_on\n");

	for (vector<string>::const_iterator name = order.begin(); name != order.end(); ++name) {
		const set<placement>& s = tracks[*name];
		if (!as_csv) {
			/* Time covered by at least one region: the channels of a
			 * multichannel track overlap and must not add up. */
			int64_t covered = 0, run_start = 0, run_end = -1;
			for (set<placement>::const_iterator p = s.begin(); p != s.end(); ++p) {
				int64_t end = p->start + p->length;
				if (p->start > run_end) {
					if (run_end >= 0)
						covered += run_end - run_start;
					run_start = p->start;
					run_end = end;
				} else if (end > run_end) {
					run_end = end;
				}
			}
			if (run_end >= 0)
				covered += run_end - run_start;
			printf("%s: %zu region%s, %s covered\n", name->c_str(), s.size(),
				s.size() == 1 ? "" : "s", clock(covered, rate).c_str());
		}
		for (set<placement>::const_iterator p = s.begin(); p != s.end(); ++p) {
			int64_t end = p->start + p->length;
			if (as_csv) {
				printf("%s,%s,%s,%" PRId64 ",%s,%" PRId64 ",%s,%" PRId64 ",%" PRId64 ",%s,%g,%s,%s,%s\n",
					csv(*name).c_str(), csv(p->region).c_str(), csv(p->file).c_str(),
					p->start, clock(p->start, rate).c_str(),
					end, clock(end, rate).c_str(), p->length, p->offset,
					date(p->created, "%Y-%m-%dT%H:%M:%SZ").c_str(), p->gain,
					timecode(p->start, rate, fpsnum, fpsden).c_str(),
					timecode(end, rate, fpsnum, fpsden).c_str(),
					csv(p->recorded_on).c_str());
			} else {
				/* Timecode when the frame rate is known, clock time if not. */
				string from = timecode(p->start, rate, fpsnum, fpsden);
				string to = timecode(end, rate, fpsnum, fpsden);
				if (from.empty() || to.empty()) {
					from = clock(p->start, rate);
					to = clock(end, rate);
				}
				printf("  %s - %s  %-30s %s%s%s",
					from.c_str(), to.c_str(),
					p->region.c_str(), p->file.c_str(),
					p->created ? "  created " : "",
					date(p->created, "%Y-%m-%d %H:%M UTC").c_str());
				if (p->gain != 0)
					printf("  gain %+g dB", p->gain);
				printf("\n");
			}
		}
		if (!as_csv)
			printf("\n");
	}
	return missing ? 2 : 0;
}
