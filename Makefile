all:
	$(CXX) -o ptftool ptftool.cc ptfformat.cc
	$(CC) -o ptunxor ptunxor.c
clean:
	rm ptftool ptunxor
