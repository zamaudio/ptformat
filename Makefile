all:
	$(CC) ptunxor.c -o ptunxor
	$(CC) ptparse.c -o ptparse

clean:
	rm ptunxor ptparse
