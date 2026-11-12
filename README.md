ptformat [![badge](https://github.com/zamaudio/ptformat/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/zamaudio/ptformat/actions)
========

ptformat reads and parses [ProTools](https://www.avid.com/pro-tools) session files.

Audio and MIDI source/region/track information is extracted, as well as MIDI note events.

The idea is to make [ardour](https://ardour.org/) open PT sessions.

Current functionality
===

Decrypt and parse a PT file:

	make
	./ptftool file.pt{s,5,f,x}

`./ptftool -v file.pt{s,5,f,x}` also prints the bit depth, the frame
rate, the fades, the video files, where the
session was saved, the creation date of each audio file, the channel
and UUID of each region, the memory locations, the volumes and plugins
the session uses, every track with its type (audio, aux, master or
video), the plugin in each insert slot, the output and sends of each
mixer strip and the paths of the I/O setup.

API
===

See ptftool.cc for example usage

What works?
===========

| **PT version** | **Decryption** | **Audio (Sources)** | **Audio (Regions)** | **Audio (Tracks)**| **MIDI (Chunks)** | **MIDI (Regions)** | **MIDI (Tracks)** |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 5 | Yes | Yes | Yes | Yes | ? | ? | ? |
| 6 | Yes | Yes | Yes | Yes | ? | ? | ? |
| 7 | Yes | Yes | Yes | Yes | ? | ? | ? |
| 8 | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| 9 | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
|10 | Yes | Yes | No groups | Yes | Yes | No groups | Yes |
|11 | Yes | Yes | No groups | Yes | Yes | No groups | Yes |
|12 | Yes | Yes | No groups | Yes | Yes | No groups | Yes |


Regression testing
==================

To test that nothing has broken since code has been changed:

	make
	./ptreg


Dummy audio file generation
===========================

To make a sox script for regenerating all audio in a PT session as dummy wavs:

	make
	./ptgenmissing file.pt{s,5,f,x}


Missing and unused media
========================

To list the audio files a session references that are not on disk, and
the audio files on disk that the session does not reference:

	make
	./ptcheckmedia file.pt{s,5,f,x} [audio dir ...]

With no audio dir, `Audio Files` next to the session is searched. Exits
with 1 when files are missing.


Comparing two saves of a session
================================

To list the regions added, removed, renamed and trimmed between two saves
of a session, and the track placements added, removed and moved:

	make
	./ptdiff old.pt{s,5,f,x} new.pt{s,5,f,x}

Regions are matched by the UUID PT12 sessions store for each region;
PT5 and PT8 sessions have none and are not supported. Exits with 1 when
there are differences.


Regions per track
=================

To list the regions placed on each audio track in time order, with
start and end positions (as timecode when the session holds a frame
rate), or only the tracks named:

	make
	./pttracks [--csv] file.pt{s,5,f,x} [track name ...]

Each track shows the time covered by its regions, and each region the
date its file was created (UTC), when the session holds it, and its clip
gain when it is not 0 dB. `--csv` prints one row per region.


Hacking
=======

To decrypt a PT session for further inspection or adding features:

	make
	./ptunxor file.pt{s,5,f,x} > file.unxor


License
=======

### LGPLv2.1+


TODO
====

- Add >= PT10 Compound MIDI/Audio region support


Binaries in `bins/`
===================

The binaries located in `bins/` directory are specially crafted test sessions
for regression testing this library.  They are not programs!
