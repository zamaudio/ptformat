INCL=$(shell pkg-config --cflags glib-2.0) -I.
INCL32=$(shell PKG_CONFIG_PATH=/usr/lib/pkgconfig pkg-config --cflags glib-2.0) -I.
STRICT=-Wall -Wcast-align -Wextra -Wwrite-strings -Wunsafe-loop-optimizations -Wlogical-op -Wno-unused-function -Wno-implicit-fallthrough -std=c++98
CLANGSTRICT=-Woverloaded-virtual -Wno-mismatched-tags -ansi -Wnon-virtual-dtor -Woverloaded-virtual -fstrict-overflow -Wall -Wcast-align -Wextra -Wwrite-strings -Wno-unused-function -std=c++98

all:
	$(CXX) -o ptftool -g ${INCL} ${STRICT} ptftool.cc ptfformat.cc
	$(CXX) -o ptunxor -g ${INCL} ${STRICT} ptunxor.cc ptfformat.cc
	$(CXX) -o ptgenmissing -g ${INCL} ${STRICT} ptgenmissing.cc ptfformat.cc

all32:
	$(CXX) -m32 -o ptftool -g ${INCL32} ${STRICT} ptftool.cc ptfformat.cc
	$(CXX) -m32 -o ptunxor -g ${INCL32} ${STRICT} ptunxor.cc ptfformat.cc
	$(CXX) -m32 -o ptgenmissing -g ${INCL32} ${STRICT} ptgenmissing.cc ptfformat.cc

clangall:
	clang++ -o ptftool -g ${INCL} ${CLANGSTRICT} ptftool.cc ptfformat.cc
	clang++ -o ptunxor -g ${INCL} ${CLANGSTRICT} ptunxor.cc ptfformat.cc
	clang++ -o ptgenmissing -g ${INCL} ${CLANGSTRICT} ptgenmissing.cc ptfformat.cc
	
clean:
	rm ptftool ptunxor ptgenmissing
