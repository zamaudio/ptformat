ptf2ardour
==========

Idea is to make ardour open ptf sessions.

First step, decrypt the .ptf files.

Usage:

	make
	./ptunxor file.ptf > file.out
	./ptparse file.out

Outputs WAV filenames and start offset in samples
to recording per track where file was created.
