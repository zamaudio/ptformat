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
 * ptpan: write a copy of a session with a panner setting of a mixer slot
 * of a track changed.
 *
 * Usage: ptpan in.ptx out.ptx track slot setting value
 *
 * slot is "out" for the track's output or a letter A to J for a send.
 * setting is "pan" (left/right position of the mono or left channel,
 * -100 to 100), "pan-right" (the right channel of a stereo strip),
 * "height" or "size" (0 to 100; a mono strip with a surround or object
 * panner only). The session read is never written: out must be a new
 * file. A setting that holds automation is refused.
 *
 * Exit status: 0 success, 1 no such track or slot, 2 usage, read,
 * write error or refused change.
 */

#include "ptformat/ptformat.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace std;

int main (int argc, char **argv) {
	if (argc != 7) {
		fprintf(stderr, "Usage: %s in.ptx out.ptx track out|A-J pan|pan-right|height|size value\n", argv[0]);
		return 2;
	}
	const char *in = argv[1], *out = argv[2];
	string track = argv[3], slotname = argv[4], setting = argv[5];
	int slot;
	if (slotname == "out")
		slot = 0;
	else if (slotname.size() == 1 && slotname[0] >= 'A' && slotname[0] <= 'J')
		slot = slotname[0] - 'A' + 1;
	else {
		fprintf(stderr, "Slot must be out or a letter A to J\n");
		return 2;
	}
	PTFFormat::pan_param param;
	if (setting == "pan")
		param = PTFFormat::PAN_LEFT_RIGHT;
	else if (setting == "pan-right")
		param = PTFFormat::PAN_LEFT_RIGHT_2;
	else if (setting == "height")
		param = PTFFormat::PAN_HEIGHT;
	else if (setting == "size")
		param = PTFFormat::PAN_SIZE;
	else {
		fprintf(stderr, "Setting must be pan, pan-right, height or size\n");
		return 2;
	}
	char *end;
	long value = strtol(argv[6], &end, 10);
	bool position = (param == PTFFormat::PAN_LEFT_RIGHT || param == PTFFormat::PAN_LEFT_RIGHT_2);
	if (*end || end == argv[6] || value > 100 || value < (position ? -100 : 0)) {
		fprintf(stderr, "Value must be a whole number from %d to 100\n", position ? -100 : 0);
		return 2;
	}

	FILE *f = fopen(out, "rb");
	if (f) {
		fclose(f);
		fprintf(stderr, "%s exists; give a new file\n", out);
		return 2;
	}

	PTFFormat ptf;
	int ok = ptf.load(in, 48000);
	if (ok != 0) {
		fprintf(stderr, "Cannot read session %s (error %d)\n", in, ok);
		return 2;
	}

	/* Routes of one slot share it: change it once. */
	const PTFFormat::route_t *hit = NULL;
	for (vector<PTFFormat::route_t>::const_iterator r = ptf.routes().begin();
			r != ptf.routes().end(); ++r) {
		if (r->strip == track && r->slot == slot) {
			hit = &*r;
			break;
		}
	}
	if (!hit) {
		fprintf(stderr, "No assigned slot %s on %s\n", slotname.c_str(), track.c_str());
		return 1;
	}
	PTFFormat::route_t route = *hit;
	if (!ptf.set_pan(route, param, (int32_t)value)) {
		fprintf(stderr, "Cannot set %s of slot %s of %s: the slot has no such panner setting, "
			"it holds automation, or it is not as expected\n", setting.c_str(), slotname.c_str(), track.c_str());
		return 2;
	}
	if (ptf.save(out) != 0) {
		fprintf(stderr, "Cannot write %s\n", out);
		return 2;
	}
	printf("Set %s of slot %s of %s to %ld\n", setting.c_str(), slotname.c_str(), track.c_str(), value);
	return 0;
}
