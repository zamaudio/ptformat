ptformat [![travis](https://travis-ci.org/zamaudio/ptformat.svg?branch=master)](https://travis-ci.org/zamaudio/ptformat)
========
ptformat reads and parses ProTools session files.

Audio and MIDI source/region/track information is extracted, as well as MIDI note events.

The idea is to make ardour open PT sessions.

Current functionality:

Decrypt and parse a PT file:

	make
	./ptftool file.pt{s,5,f,x}

API
===
See ptftool.cc for example usage

What works?
===========

| **PT version** | **Decryption** | **Audio (Sources)** | **Audio (Regions)** | **Audio (Tracks)**| **MIDI (Chunks)** | **MIDI (Regions)** | **MIDI (Tracks)** |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 5 | Yes | Yes | Yes | Yes | ? | ? | ? |
| 6 | Yes | ? | ? | ? | ? | ? | ? |
| 7 | Yes | ? | ? | ? | ? | ? | ? |
| 8 | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| 9 | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
|10 | Yes | Yes | No groups | Yes | Yes | Yes | Yes |
|11 | Yes | Yes | No groups | Yes | Yes | Yes | Yes |
|12 | Yes | Yes | No groups | Yes | Yes | Yes | Yes |


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

- PT6 support (?)

- PT7 support


Please attach more ptx session files for development of this library!
=====================================================================
