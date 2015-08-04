all:
	$(CXX) -o ptftool -g -Wall ptftool.cc ptfformat.cc
	$(CXX) -o ptunxor -g -Wall ptunxor.cc ptfformat.cc
clean:
	rm ptftool ptunxor
