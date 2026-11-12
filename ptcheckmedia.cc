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
 * the session references but no dir contains (missing), and audio files
 * in the dirs the session does not reference (unused). The session file
 * and the dirs are only read.
 *
 * Exit status: 0 nothing missing, 1 files missing, 2 usage or read error.
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

/* Collect regular entry names of dir into names; false if dir is unreadable. */
static bool
list_dir(const string& dir, set<string>& names)
{
	DIR *d = opendir(dir.c_str());
	if (!d)
		return false;
	struct dirent *e;
	while ((e = readdir(d)) != 0) {
		/* Skip ".", ".." and AppleDouble "._*" metadata files. */
		if (e->d_name[0] == '.')
			continue;
		names.insert(e->d_name);
	}
	closedir(d);
	return true;
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

	set<string> on_disk;
	for (vector<string>::const_iterator d = dirs.begin(); d != dirs.end(); ++d) {
		if (!list_dir(*d, on_disk)) {
			fprintf(stderr, "Cannot read directory %s\n", d->c_str());
			return 2;
		}
	}

	/* File name -> creation time (0 when the session holds none). */
	map<string, int64_t> referenced;
	for (vector<PTFFormat::wav_t>::const_iterator a = ptf.audiofiles().begin();
			a != ptf.audiofiles().end(); ++a) {
		referenced[a->filename] = a->created;
	}

	vector<string> missing;
	for (map<string, int64_t>::const_iterator r = referenced.begin(); r != referenced.end(); ++r) {
		if (on_disk.find(r->first) == on_disk.end())
			missing.push_back(r->first);
	}

	vector<string> unused;
	for (set<string>::const_iterator f = on_disk.begin(); f != on_disk.end(); ++f) {
		if (is_audio_name(*f) && referenced.find(*f) == referenced.end())
			unused.push_back(*f);
	}

	printf("Session: %s\n", argv[1]);
	for (vector<string>::const_iterator d = dirs.begin(); d != dirs.end(); ++d)
		printf("Searched: %s\n", d->c_str());
	printf("Referenced: %zu, missing: %zu, unused on disk: %zu\n",
		referenced.size(), missing.size(), unused.size());

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

	printf("\nUnused (on disk, not referenced):\n");
	for (vector<string>::const_iterator u = unused.begin(); u != unused.end(); ++u)
		printf("%s\n", u->c_str());

	return missing.empty() ? 0 : 1;
}
