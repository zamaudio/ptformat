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

| **PT version** | **Multitrack Audio** | **Multitrack MIDI** |
| --- | --- | --- |
| 5 | Partially | ? |
| 6 | ? | ? |
| 7 | ? | ? |
| 8 | Full support | Full support |
| 9 | Full support | Full support |
| 10 | Partially | Partially |
| 11 | Partially | ? |
| 12 | Full support | Full support |


Regression testing
==================

To test that nothing has broken since code has been changed:

	make
	./ptreg

TODO
====

- PT6 support (?)

- PT7 support
