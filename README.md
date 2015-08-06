ptformat
=========

ptformat currently opens *every* .ptf file
above and including version 8 but does not yet open any .ptx files.

Audio source/region/track information is extracted.

The idea is to make ardour open pt sessions.

Current functionality:

Decrypt and parse a .ptf file:

	make
	./ptftool file.ptf
