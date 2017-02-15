STRICT=-Wall -Wcast-align -Wextra -Wwrite-strings -Wunsafe-loop-optimizations -Wlogical-op -Wno-unused-function
CLANGSTRICT=-Woverloaded-virtual -Wno-mismatched-tags -ansi -Wnon-virtual-dtor -Woverloaded-virtual -fstrict-overflow -Wall -Wcast-align -Wextra -Wwrite-strings -Wno-unused-function

all:
	$(CXX) -o ptftool -g ${STRICT} ptftool.cc ptfformat.cc
	$(CXX) -o ptunxor -g ${STRICT} ptunxor.cc ptfformat.cc

clangall:
	clang++ -o ptftool -g ${CLANGSTRICT} ptftool.cc ptfformat.cc
	clang++ -o ptunxor -g ${CLANGSTRICT} ptunxor.cc ptfformat.cc
	
clean:
	rm ptftool ptunxor
