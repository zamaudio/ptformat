all:
	$(CXX) -o ptftool -g -Wall ptftool.cc ptfformat.cc
	$(CC) -o ptunxor -g ptunxor.c
clean:
	rm ptftool ptunxor
