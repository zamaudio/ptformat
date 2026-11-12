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
 * With track names, only those tracks are listed. Positions are in
 * samples and in h:mm:ss.sss from the start of the timeline. Each track
 * shows the time covered by at least one of its regions, so channels of
 * a multichannel track that overlap count once. --csv prints one row
 * per region with a header line.
 *
 * Exit status: 0 success, 2 usage or read error or a track name not
 * found.
 */

#include "ptformat/ptformat.h"
#include <inttypes.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
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
	int64_t rate = ptf.sessionrate() ? ptf.sessionrate() : 48000;

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
		printf("track,region,file,start,start_time,end,end_time,length,file_offset\n");

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
				printf("%s,%s,%s,%" PRId64 ",%s,%" PRId64 ",%s,%" PRId64 ",%" PRId64 "\n",
					csv(*name).c_str(), csv(p->region).c_str(), csv(p->file).c_str(),
					p->start, clock(p->start, rate).c_str(),
					end, clock(end, rate).c_str(), p->length, p->offset);
			} else {
				printf("  %s - %s  %-30s %s\n",
					clock(p->start, rate).c_str(), clock(end, rate).c_str(),
					p->region.c_str(), p->file.c_str());
			}
		}
		if (!as_csv)
			printf("\n");
	}
	return missing ? 2 : 0;
}
