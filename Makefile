INCL=$(shell pkg-config --cflags glib-2.0)
STRICT=-Wall -Wcast-align -Wextra -Wwrite-strings -Wunsafe-loop-optimizations -Wlogical-op -Wno-unused-function -Wno-implicit-fallthrough
CLANGSTRICT=-Woverloaded-virtual -Wno-mismatched-tags -ansi -Wnon-virtual-dtor -Woverloaded-virtual -fstrict-overflow -Wall -Wcast-align -Wextra -Wwrite-strings -Wno-unused-function

all:
	$(CXX) -o ptftool -g ${INCL} ${STRICT} ptftool.cc ptfformat.cc
	$(CXX) -o ptunxor -g ${INCL} ${STRICT} ptunxor.cc ptfformat.cc

clangall:
	clang++ -o ptftool -g ${INCL} ${CLANGSTRICT} ptftool.cc ptfformat.cc
	clang++ -o ptunxor -g ${INCL} ${CLANGSTRICT} ptunxor.cc ptfformat.cc
	
clean:
	rm ptftool ptunxor
