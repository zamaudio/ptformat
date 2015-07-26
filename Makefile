all:
	$(CXX) -o ptftool -g ptftool.cc ptfformat.cc
	$(CC) -o ptunxor -g ptunxor.c
clean:
	rm ptftool ptunxor
