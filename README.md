ptformat
=========

ptformat currently opens *every* .ptf file
but does not yet open any .ptx files.

Audio source/region/track information is extracted.

The idea is to make ardour open pt sessions.

Current functionality:

Decrypt and parse any .ptf file:

	make
	./ptftool file.ptf
