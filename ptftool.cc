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

#include "ptformat/ptformat.h"
#include <inttypes.h> // PRIxyy
#include <cstdio>
#include <cstring>
#include <ctime>
#include <stdlib.h>

using namespace std;
using std::string;

/* Path components as "/volume/folder/file". */
static string
join(const vector<string>& parts)
{
	string s;
	for (size_t i = 0; i < parts.size(); i++)
		s += "/" + parts[i];
	return s;
}

int main (int argc, char **argv) {
	PTFFormat ptf;
	int ok;

	/* -v adds what the default output leaves out (bit depth, saved paths,
	 * file creation dates, region channels and UUIDs); without it the
	 * output stays as the regression tests expect. */
	bool verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
	int arg = verbose ? 2 : 1;

	if (argc <= arg) {
		printf("No ptf file specified, quit\n");
		exit(0);
	}

	ok = ptf.load(argv[arg], 48000);

	switch (ok) {
	default:
	case -1:
		printf("Cannot decrypt ptf, quit\n");
		exit(-1);
		break;
	case -2:
		printf("Cannot extract version from ptf, quit\n");
		exit(-1);
		break;
	case -3:
		printf("Unsupported ptf version, quit\n");
		exit(-1);
		break;
	case -4:
		printf("Cannot parse ptf, quit\n");
		exit(-1);
		break;
	case 0:
		printf("ProTools %d Session: Samplerate = %" PRId64 "Hz\nTarget samplerate = 48000\n\n", ptf.version(), ptf.sessionrate());
		if (verbose) {
			printf("Bit depth = %d%s\n", ptf.sessionbitdepth(), ptf.sessionfloat() ? " float" : "");
			if (ptf.framerateden())
				printf("Frame rate = %u/%u (%.3f fps%s)\n", ptf.frameratenum(), ptf.framerateden(),
					(double)ptf.frameratenum() / ptf.framerateden(), ptf.dropframe() ? ", drop frame" : "");
			if (!ptf.sessionpath().empty())
				printf("Saved at = %s\n", join(ptf.sessionpath()).c_str());
			if (!ptf.sourcepath().empty())
				printf("Source session = %s\n", join(ptf.sourcepath()).c_str());
			printf("\n");
		}
		printf("%zu wavs, %zu regions, %zu active regions\n\n",
			ptf.audiofiles().size(),
			ptf.regions().size(),
			ptf.tracks().size()
			);
		printf("Audio file (WAV#) @ offset, length:\n");
		for (vector<PTFFormat::wav_t>::const_iterator
				a = ptf.audiofiles().begin();
				a != ptf.audiofiles().end(); ++a) {
			printf("`%s` w(%d) @ %" PRIu64 ", %" PRIu64,
				a->filename.c_str(),
				a->index,
				a->posabsolute,
				a->length);
			if (verbose && a->created) {
				char buf[32];
				time_t t = (time_t)a->created;
				strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", gmtime(&t));
				printf(" created %s UTC", buf);
			}
			printf("\n");
		}

		printf("\nRegion (Region#) (WAV#) @ into-sample, length:\n");
		for (vector<PTFFormat::region_t>::const_iterator
				a = ptf.regions().begin();
				a != ptf.regions().end(); ++a) {
			printf("`%s` r(%d) w(%d) @ %" PRIu64 ", %" PRIu64,
				a->name.c_str(),
				a->index,
				a->wave.index,
				a->sampleoffset,
				a->length);
			if (verbose) {
				printf(" c(%u)", a->channel);
				if (!a->uid.empty())
					printf(" uid(%s)", a->uid.c_str());
				if (a->gain != 0)
					printf(" gain(%g dB)", a->gain);
				if (!a->recorded_on.empty())
					printf(" rec(%s)", a->recorded_on.c_str());
			}
			printf("\n");
		}

		printf("\nMIDI Region (Region#) @ into-sample, length:\n");
		for (vector<PTFFormat::region_t>::const_iterator
			a = ptf.midiregions().begin();
			a != ptf.midiregions().end(); ++a) {
			printf("`%s` r(%d) @ %" PRIu64 ", %" PRIu64 "\n",
				a->name.c_str(),
				a->index,
				a->sampleoffset,
				a->length);
			for (vector<PTFFormat::midi_ev_t>::const_iterator
					b = a->midi.begin();
					b != a->midi.end(); ++b) {
				printf("    MIDI: n(%d) v(%d) @ %" PRIu64 ", %" PRIu64 "\n",
					b->note, b->velocity,
					b->pos, b->length);
			}
		}

		printf("\nTrack name (Track#) (Region#) @ Absolute:\n");
		for (vector<PTFFormat::track_t>::const_iterator
				a = ptf.tracks().begin();
				a != ptf.tracks().end(); ++a) {
			printf("`%s` t(%d) r(%d) @ %" PRIu64 "\n",
				a->name.c_str(),
				a->index,
				a->reg.index,
				a->reg.startpos);
		}

		printf("\nMIDI Track name (MIDITrack#) (MIDIRegion#) @ Absolute:\n");
		for (vector<PTFFormat::track_t>::const_iterator
				a = ptf.miditracks().begin();
				a != ptf.miditracks().end(); ++a) {
			printf("`%s` mt(%d) mr(%d) @ %" PRIu64 "\n",
				a->name.c_str(),
				a->index,
				a->reg.index,
				a->reg.startpos);
		}

		printf("\nTrack name (Track#) (WAV filename) @ Absolute + Into-sample, Length:\n");
		for (vector<PTFFormat::track_t>::const_iterator
				a = ptf.tracks().begin();
				a != ptf.tracks().end(); ++a) {
			printf("`%s` t(%d) (%s) @ %" PRIu64 " + %" PRIu64 ", %" PRIu64 "\n",
				a->name.c_str(),
				a->index,
				a->reg.wave.filename.c_str(),
				a->reg.startpos,
				a->reg.sampleoffset,
				a->reg.length
				);
		}
		if (verbose) {
			printf("\nMarker (Marker#) @ start, end:\n");
			for (vector<PTFFormat::marker_t>::const_iterator
					m = ptf.markers().begin();
					m != ptf.markers().end(); ++m) {
				printf("`%s` m(%d) @ %" PRId64 ", %" PRId64 "\n",
					m->name.c_str(), m->index, m->start, m->end);
			}

			/* Type 1 crossfade, 2 fade-out, 3 fade-in. */
			printf("\nFade (track) type shape @ start, length:\n");
			for (vector<PTFFormat::fade_t>::const_iterator
					fd = ptf.fades().begin();
					fd != ptf.fades().end(); ++fd) {
				printf("`%s` %s %s @ %" PRId64 ", %" PRId64 "\n", fd->track.c_str(),
					fd->type == 1 ? "cross" : fd->type == 2 ? "out" : fd->type == 3 ? "in" : "?",
					fd->shape == 1 ? "power" : fd->shape == 2 ? "linear" : "?",
					fd->start, fd->length);
			}

			printf("\nVideo file: fps, frames, size, created\n");
			for (vector<PTFFormat::video_t>::const_iterator
					vf = ptf.videofiles().begin();
					vf != ptf.videofiles().end(); ++vf) {
				char buf[32] = "-";
				if (vf->created) {
					time_t t = (time_t)vf->created;
					strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S UTC", gmtime(&t));
				}
				printf("`%s`: %.3f, %u, %ux%u, %s\n", vf->filename.c_str(), vf->fps,
					vf->frames, vf->width, vf->height, buf);
			}

			printf("\nVolume: mount path\n");
			for (vector<PTFFormat::volume_t>::const_iterator
					vol = ptf.volumes().begin();
					vol != ptf.volumes().end(); ++vol) {
				printf("`%s`: %s\n", vol->name.c_str(), vol->path.c_str());
			}

			/* Every track: 0 audio, 1 MIDI, 2 aux, 5 master, 8 video. */
			printf("\nStrip (type) (format) order, playlist:\n");
			for (vector<PTFFormat::strip_t>::const_iterator
					st = ptf.strips().begin();
					st != ptf.strips().end(); ++st) {
				printf("`%s` (%d) (%d) %d, %s\n", st->name.c_str(), st->type, st->format, st->order, st->playlist.c_str());
			}

			printf("\nGroup: tracks\n");
			for (vector<PTFFormat::group_t>::const_iterator
					g = ptf.groups().begin();
					g != ptf.groups().end(); ++g) {
				printf("`%s`:", g->name.c_str());
				for (vector<string>::const_iterator t = g->tracks.begin();
						t != g->tracks.end(); ++t)
					printf("%s `%s`", t == g->tracks.begin() ? "" : ",", t->c_str());
				printf("\n");
			}

			printf("\nPlugin (codes) bundle:\n");
			for (vector<PTFFormat::plugin_t>::const_iterator
					p = ptf.plugins().begin();
					p != ptf.plugins().end(); ++p) {
				printf("`%s` (%s) %s\n", p->name.c_str(), p->codes.c_str(), p->bundle.c_str());
			}

			printf("\nInsert (strip) (Strip#) slot: plugin\n");
			for (vector<PTFFormat::insert_t>::const_iterator
					i = ptf.inserts().begin();
					i != ptf.inserts().end(); ++i) {
				printf("`%s` s(%d) %c: %s\n", i->strip.c_str(), i->strip_index, 'A' + i->slot,
					i->plugin.name.empty() ? "?" : i->plugin.name.c_str());
			}

			/* Slot 0 looks like the main output, 1 to 10 like sends A-J. */
			printf("\nRoute (strip) (Strip#) slot: destination, level, pan\n");
			for (vector<PTFFormat::route_t>::const_iterator
					r = ptf.routes().begin();
					r != ptf.routes().end(); ++r) {
				if (r->slot == 0)
					printf("`%s` s(%d) out: %s", r->strip.c_str(), r->strip_index, r->destination.c_str());
				else
					printf("`%s` s(%d) send %c: %s", r->strip.c_str(), r->strip_index, 'A' + r->slot - 1, r->destination.c_str());
				printf(", %+.1f dB%s, pan %d/%d\n", r->level, r->muted ? " muted" : "", r->pan, r->pan_right);
			}

			/* Lanes with a single point hold a fixed value: skip them. */
			printf("\nAutomation (strip) slot lane: points (position, value)\n");
			for (vector<PTFFormat::automation_t>::const_iterator
					au = ptf.automation().begin();
					au != ptf.automation().end(); ++au) {
				if (au->points.size() < 2)
					continue;
				if (au->pan)
					printf("`%s` s(%d) slot %d pan %c:", au->strip.c_str(), au->strip_index, au->slot, au->pan == 1 ? 'L' : 'R');
				else
					printf("`%s` s(%d) slot %d lane %d:", au->strip.c_str(), au->strip_index, au->slot, au->lane);
				for (size_t i = 0; i < au->points.size(); i++)
					printf(" (%" PRId64 ", %g)", au->points[i].pos, au->points[i].value);
				printf("\n");
			}

			printf("\nTempo @ ticks: bpm\n");
			for (vector<PTFFormat::tempo_t>::const_iterator
					t = ptf.tempos().begin();
					t != ptf.tempos().end(); ++t) {
				printf("@ %" PRId64 ": %g\n", t->pos, t->bpm);
			}

			printf("\nI/O path (list) channels, parent:\n");
			for (vector<PTFFormat::iopath_t>::const_iterator
					io = ptf.iopaths().begin();
					io != ptf.iopaths().end(); ++io) {
				printf("`%s` (%s) %u%s%s\n", io->name.c_str(), io->list == 0x2603 ? "out" : "in",
					io->channels, io->parent.empty() ? "" : ", ", io->parent.c_str());
			}
		}
		break;
	}
	exit(0);
}
