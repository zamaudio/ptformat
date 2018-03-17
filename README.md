ptformat
=========
ptformat currently opens PT{5,8,9,10,11} files.

Audio source/region/track information is extracted.

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
| 10 | Full support | Partially |
| 11 | Partially | ? |
| 12 | ? | ? |


TODO
====

- PT6 support (?)

- PT7 support

- Investigate PT12 (feel free to send me some .ptx ProTools 12 session files)

