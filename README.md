ptfformat
=========

ptfformat currently opens any .ptf file
and collects audio source/region/track information.
(PT9 .ptf not supported yet)

The idea is to make ardour open ptf sessions.

First step, decrypt and parse the .ptf files.

Usage:

	make
	./ptftool file.ptf

There are no more missing lookups!
