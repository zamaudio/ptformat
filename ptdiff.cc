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
 * ptdiff: compare two saves of a session region by region.
 *
 * Usage: ptdiff old.pt{s,5,f,x} new.pt{s,5,f,x}
 *
 * Regions are matched by the UUID PT12 sessions store for each region,
 * which stays the same across saves. Pro Tools gives a trimmed region a
 * new UUID, and some saves renew every UUID, so a region removed and one
 * added with the same name and file are taken as the same region:
 * trimmed if its offset or length changed, unchanged otherwise. Reports
 * regions added,
 * removed, renamed and trimmed (offset, length or file changed), and
 * audio track placements added, removed and moved. Positions are in
 * samples and in h:mm:ss.sss from the start of the timeline.
 *
 * Exit status: 0 no differences, 1 differences, 2 usage or read error.
 */

#include "ptformat/ptformat.h"
#include <inttypes.h>
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace std;

typedef map<string, PTFFormat::region_t> region_map;
/* (uid, track name) -> start positions of that region on that track */
typedef map<pair<string, string>, vector<int64_t> > placement_map;

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

static bool
load(PTFFormat& ptf, const char *path, region_map& regions, placement_map& placed)
{
	int ok = ptf.load(path, 48000);
	if (ok != 0) {
		fprintf(stderr, "Cannot read session %s (error %d)\n", path, ok);
		return false;
	}
	for (vector<PTFFormat::region_t>::const_iterator r = ptf.regions().begin();
			r != ptf.regions().end(); ++r) {
		if (!r->uid.empty())
			regions[r->uid] = *r;
	}
	for (vector<PTFFormat::track_t>::const_iterator t = ptf.tracks().begin();
			t != ptf.tracks().end(); ++t) {
		if (!t->reg.uid.empty())
			placed[make_pair(t->reg.uid, t->name)].push_back(t->reg.startpos);
	}
	if (regions.empty() && !ptf.regions().empty()) {
		fprintf(stderr, "Session %s stores no region ids (PT12 sessions do)\n", path);
		return false;
	}
	return true;
}

static void
print_region(const char *tag, const PTFFormat::region_t& r)
{
	printf("%s `%s` (%s) offset %" PRId64 ", length %" PRId64 "\n",
		tag, r.name.c_str(), r.wave.filename.c_str(), r.sampleoffset, r.length);
}

int main (int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage: %s old.pt{s,5,f,x} new.pt{s,5,f,x}\n", argv[0]);
		return 2;
	}

	PTFFormat a, b;
	region_map ra, rb;
	placement_map pa, pb;
	if (!load(a, argv[1], ra, pa) || !load(b, argv[2], rb, pb))
		return 2;
	int64_t rate = b.sessionrate() ? b.sessionrate() : 48000;

	/* Regions only in one save, and regions kept with the same id. */
	vector<PTFFormat::region_t> removed, added;
	int nren = 0, ntrim = 0, nreid = 0;

	printf("Regions:\n");
	for (region_map::const_iterator r = ra.begin(); r != ra.end(); ++r) {
		region_map::const_iterator o = rb.find(r->first);
		if (o == rb.end()) {
			removed.push_back(r->second);
			continue;
		}
		if (o->second.name != r->second.name) {
			printf("  renamed `%s` -> `%s`\n", r->second.name.c_str(), o->second.name.c_str());
			nren++;
		}
		if (o->second.sampleoffset != r->second.sampleoffset ||
				o->second.length != r->second.length ||
				o->second.wave.filename != r->second.wave.filename) {
			print_region("  trimmed", r->second);
			print_region("       to", o->second);
			ntrim++;
		}
	}
	for (region_map::const_iterator r = rb.begin(); r != rb.end(); ++r) {
		if (ra.find(r->first) == ra.end())
			added.push_back(r->second);
	}

	/* Pro Tools gives a trimmed region a new id, so a removed and an
	 * added region with the same name and file are one trimmed region. */
	for (vector<PTFFormat::region_t>::iterator r = removed.begin(); r != removed.end();) {
		vector<PTFFormat::region_t>::iterator o = added.begin();
		while (o != added.end() &&
				(o->name != r->name || o->wave.filename != r->wave.filename))
			++o;
		if (o == added.end()) {
			++r;
			continue;
		}
		/* Some saves give every region a new id; unchanged ones are not
		 * a difference. */
		if (o->sampleoffset != r->sampleoffset || o->length != r->length) {
			print_region("  trimmed", *r);
			print_region("       to", *o);
			ntrim++;
		} else {
			nreid++;
		}
		added.erase(o);
		r = removed.erase(r);
	}
	for (size_t i = 0; i < removed.size(); i++)
		print_region("  removed", removed[i]);
	for (size_t i = 0; i < added.size(); i++)
		print_region("  added", added[i]);
	int nadd = added.size(), nrem = removed.size();

	/* Placements as (region name, track name, position), minus the ones
	 * present in both saves with the same id. */
	typedef pair<pair<string, string>, int64_t> placement;
	vector<placement> pgone, pnew;
	for (placement_map::const_iterator p = pa.begin(); p != pa.end(); ++p) {
		placement_map::const_iterator q = pb.find(p->first);
		vector<int64_t> after = (q == pb.end()) ? vector<int64_t>() : q->second;
		for (size_t i = 0; i < p->second.size(); i++) {
			vector<int64_t>::iterator y = find(after.begin(), after.end(), p->second[i]);
			if (y != after.end())
				after.erase(y);
			else
				pgone.push_back(placement(make_pair(ra[p->first.first].name, p->first.second), p->second[i]));
		}
		for (size_t i = 0; i < after.size(); i++)
			pnew.push_back(placement(make_pair(rb[p->first.first].name, p->first.second), after[i]));
	}
	for (placement_map::const_iterator q = pb.begin(); q != pb.end(); ++q) {
		if (pa.find(q->first) != pa.end())
			continue;
		for (size_t i = 0; i < q->second.size(); i++)
			pnew.push_back(placement(make_pair(rb[q->first.first].name, q->first.second), q->second[i]));
	}

	/* A placement gone and one new with the same region name on the same
	 * track: kept in place (a trim) or moved. */
	int padd = 0, prem = 0, pmov = 0;
	printf("\nTrack placements:\n");
	for (vector<placement>::iterator g = pgone.begin(); g != pgone.end();) {
		vector<placement>::iterator n = pnew.begin();
		while (n != pnew.end() && n->first != g->first)
			++n;
		if (n == pnew.end()) {
			++g;
			continue;
		}
		if (n->second != g->second) {
			printf("  moved   `%s` on `%s`: %" PRId64 " (%s) -> %" PRId64 " (%s)\n",
				g->first.first.c_str(), g->first.second.c_str(),
				g->second, clock(g->second, rate).c_str(),
				n->second, clock(n->second, rate).c_str());
			pmov++;
		}
		pnew.erase(n);
		g = pgone.erase(g);
	}
	for (size_t i = 0; i < pgone.size(); i++) {
		printf("  removed `%s` from `%s` @ %" PRId64 " (%s)\n",
			pgone[i].first.first.c_str(), pgone[i].first.second.c_str(),
			pgone[i].second, clock(pgone[i].second, rate).c_str());
		prem++;
	}
	for (size_t i = 0; i < pnew.size(); i++) {
		printf("  added   `%s` to `%s` @ %" PRId64 " (%s)\n",
			pnew[i].first.first.c_str(), pnew[i].first.second.c_str(),
			pnew[i].second, clock(pnew[i].second, rate).c_str());
		padd++;
	}

	printf("\nRegions: %d added, %d removed, %d renamed, %d trimmed\n", nadd, nrem, nren, ntrim);
	if (nreid)
		printf("(%d unchanged regions got a new id, not counted)\n", nreid);
	printf("Track placements: %d added, %d removed, %d moved\n", padd, prem, pmov);

	return (nadd || nrem || nren || ntrim || padd || prem || pmov) ? 1 : 0;
}
