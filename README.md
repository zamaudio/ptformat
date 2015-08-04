ptfformat
=========

ptfformat currently opens 98% of .ptf files
and collects audio source/region/track information.

The idea is to make ardour open ptf sessions.

First step, decrypt and parse the .ptf files.

Usage:

	make
	./ptftool file.ptf

If the parsing fails due to missing lookup,
work out the lookup table from:

	./ptunxor file.ptf
