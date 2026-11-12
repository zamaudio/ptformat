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
 * ptcheckmedia: compare the audio files a session references with the
 * files present on disk.
 *
 * Usage: ptcheckmedia file.pt{s,5,f,x} [audio dir ...]
 *
 * With no audio dir, "<session dir>/Audio Files" is used. Reports files
 * the session references but no dir contains (missing), files whose
 * name matches but whose id (the "umid" chunk Pro Tools writes) is not
 * the one the session holds (another file), and audio files in the
 * dirs the session does not reference (unused). The session file and
 * the dirs are only read; of an audio file only its chunk headers and
 * its "umid" chunk.
 *
 * Exit status: 0 nothing missing or different, 1 files missing or
 * different, 2 usage or read error.
 */

#include "ptformat/ptformat.h"
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace std;

static bool
is_audio_name(const string& name)
{
	static const char *exts[] = { ".wav", ".aif", ".aiff", ".mxf", 0 };
	string lower(name);
	for (size_t i = 0; i < lower.size(); i++) {
		if (lower[i] >= 'A' && lower[i] <= 'Z')
			lower[i] = lower[i] - 'A' + 'a';
	}
	for (int i = 0; exts[i]; i++) {
		size_t n = strlen(exts[i]);
		if (lower.size() > n && lower.compare(lower.size() - n, n, exts[i]) == 0)
			return true;
	}
	return false;
}

/* Collect entry names of dir into names, each with the first dir that
 * holds it; false if dir is unreadable. */
static bool
list_dir(const string& dir, map<string, string>& names)
{
	DIR *d = opendir(dir.c_str());
	if (!d)
		return false;
	struct dirent *e;
	while ((e = readdir(d)) != 0) {
		/* Skip ".", ".." and AppleDouble "._*" metadata files. */
		if (e->d_name[0] == '.')
			continue;
		names.insert(make_pair(string(e->d_name), dir));
	}
	closedir(d);
	return true;
}

static uint32_t
le4(const unsigned char *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* The 8-byte id Pro Tools writes at bytes 7 to 14 of the "umid" chunk of
 * a WAV (RIFF or RF64) file, as 16 hex digits; "" when the file has no
 * such chunk or is not a WAV file. Only the chunk headers are read. */
static string
wav_uid(const string& path)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return "";
	unsigned char h[12];
	string uid;
	if (fread(h, 1, 12, f) == 12 && (!memcmp(h, "RIFF", 4) || !memcmp(h, "RF64", 4)) && !memcmp(h + 8, "WAVE", 4)) {
		uint64_t datasize = 0;	/* from ds64, for an RF64 data chunk */
		long long pos = 12;
		for (int n = 0; n < 1000; n++) {
			unsigned char c[8];
			if (fseeko(f, pos, SEEK_SET) != 0 || fread(c, 1, 8, f) != 8)
				break;
			uint64_t size = le4(c + 4);
			if (!memcmp(c, "ds64", 4) && size >= 16) {
				unsigned char b[16];
				if (fread(b, 1, 16, f) == 16)
					datasize = le4(b + 8) | ((uint64_t)le4(b + 12) << 32);
			}
			if (!memcmp(c, "data", 4) && size == 0xffffffff && datasize)
				size = datasize;
			if (!memcmp(c, "umid", 4) && size >= 15) {
				unsigned char b[15];
				if (fread(b, 1, 15, f) == 15) {
					static const char hexdigits[] = "0123456789abcdef";
					for (int k = 7; k < 15; k++) {
						uid += hexdigits[b[k] >> 4];
						uid += hexdigits[b[k] & 0x0f];
					}
				}
				break;
			}
			pos += 8 + size + (size & 1);
		}
	}
	fclose(f);
	return uid;
}

/* Path components as "/volume/folder/file", or "" when there are none. */
static string
join(const vector<string>& parts)
{
	string s;
	for (size_t i = 0; i < parts.size(); i++)
		s += "/" + parts[i];
	return s;
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

int main (int argc, char **argv) {
	PTFFormat ptf;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s file.pt{s,5,f,x} [audio dir ...]\n", argv[0]);
		return 2;
	}

	int ok = ptf.load(argv[1], 48000);
	if (ok != 0) {
		fprintf(stderr, "Cannot read session %s (error %d)\n", argv[1], ok);
		return 2;
	}

	vector<string> dirs;
	for (int i = 2; i < argc; i++)
		dirs.push_back(argv[i]);
	if (dirs.empty())
		dirs.push_back(session_dir(argv[1]) + "/Audio Files");

	map<string, string> on_disk;
	for (vector<string>::const_iterator d = dirs.begin(); d != dirs.end(); ++d) {
		if (!list_dir(*d, on_disk)) {
			fprintf(stderr, "Cannot read directory %s\n", d->c_str());
			return 2;
		}
	}

	/* File name -> creation time (0 when the session holds none). */
	map<string, int64_t> referenced;
	/* File name -> the ids the session holds for it. */
	map<string, set<string> > uids;
	for (vector<PTFFormat::wav_t>::const_iterator a = ptf.audiofiles().begin();
			a != ptf.audiofiles().end(); ++a) {
		referenced[a->filename] = a->created;
		if (!a->uid.empty())
			uids[a->filename].insert(a->uid);
	}

	vector<string> missing;
	for (map<string, int64_t>::const_iterator r = referenced.begin(); r != referenced.end(); ++r) {
		if (on_disk.find(r->first) == on_disk.end())
			missing.push_back(r->first);
	}

	/* A file on disk with the right name but another id is not the file
	 * the session recorded or imported. */
	vector<string> other;
	size_t checked = 0, noid = 0;
	for (map<string, set<string> >::const_iterator u = uids.begin(); u != uids.end(); ++u) {
		map<string, string>::const_iterator f = on_disk.find(u->first);
		if (f == on_disk.end())
			continue;
		string id = wav_uid(f->second + "/" + f->first);
		if (id.empty()) {
			noid++;
			continue;
		}
		checked++;
		if (!u->second.count(id))
			other.push_back(u->first);
	}

	vector<string> unused;
	for (map<string, string>::const_iterator f = on_disk.begin(); f != on_disk.end(); ++f) {
		if (is_audio_name(f->first) && referenced.find(f->first) == referenced.end())
			unused.push_back(f->first);
	}

	printf("Session: %s\n", argv[1]);
	/* Where Pro Tools saved it: tells which volume the media came from. */
	string saved = join(ptf.sessionpath()), source = join(ptf.sourcepath());
	if (!saved.empty())
		printf("Saved at: %s\n", saved.c_str());
	if (!source.empty() && source != saved)
		printf("Source session: %s\n", source.c_str());
	/* The volumes it used: where missing media may be. */
	for (vector<PTFFormat::volume_t>::const_iterator vol = ptf.volumes().begin();
			vol != ptf.volumes().end(); ++vol)
		printf("Volume: %s (%s)\n", vol->name.c_str(), vol->path.c_str());
	for (vector<string>::const_iterator d = dirs.begin(); d != dirs.end(); ++d)
		printf("Searched: %s\n", d->c_str());
	printf("Referenced: %zu, missing: %zu, unused on disk: %zu\n",
		referenced.size(), missing.size(), unused.size());
	printf("Checked by id: %zu, another file with the same name: %zu, on disk without an id: %zu\n",
		checked, other.size(), noid);

	/* With the file's creation date, which helps to find it elsewhere. */
	printf("\nMissing (referenced, not on disk):\n");
	for (vector<string>::const_iterator m = missing.begin(); m != missing.end(); ++m) {
		int64_t created = referenced[*m];
		if (created) {
			char buf[32];
			time_t t = (time_t)created;
			strftime(buf, sizeof buf, "%Y-%m-%d %H:%M UTC", gmtime(&t));
			printf("%s  (created %s)\n", m->c_str(), buf);
		} else {
			printf("%s\n", m->c_str());
		}
	}

	printf("\nAnother file (same name, id differs from the session's):\n");
	for (vector<string>::const_iterator o = other.begin(); o != other.end(); ++o)
		printf("%s\n", o->c_str());

	printf("\nUnused (on disk, not referenced):\n");
	for (vector<string>::const_iterator u = unused.begin(); u != unused.end(); ++u)
		printf("%s\n", u->c_str());

	return missing.empty() && other.empty() ? 0 : 1;
}
