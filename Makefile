ifdef ($(HAVE_GLIB))
INCL=$(shell pkg-config --cflags glib-2.0) -I.
INCL32=$(shell PKG_CONFIG_PATH=/usr/lib/pkgconfig pkg-config --cflags glib-2.0) -I.
else
INCL=-I.
INCL32=-I.
endif

STRICT=-Wall -Wcast-align -Wextra -Wwrite-strings -Wunsafe-loop-optimizations -Wlogical-op -Wno-unused-function -Wno-implicit-fallthrough -std=c++98
CLANGSTRICT=-Woverloaded-virtual -Wno-mismatched-tags -ansi -Wnon-virtual-dtor -Woverloaded-virtual -fstrict-overflow -Wall -Wcast-align -Wextra -Wwrite-strings -Wno-unused-function -std=c++98

all:
	$(CXX) -o ptftool -g ${INCL} ${STRICT} ptftool.cc ptformat.cc
	$(CXX) -o ptunxor -g ${INCL} ${STRICT} ptunxor.cc ptformat.cc
	$(CXX) -o ptgenmissing -g ${INCL} ${STRICT} ptgenmissing.cc ptformat.cc
	$(CXX) -o ptcheckmedia -g ${INCL} ${STRICT} ptcheckmedia.cc ptformat.cc
	$(CXX) -o ptdiff -g ${INCL} ${STRICT} ptdiff.cc ptformat.cc
	$(CXX) -o pttracks -g ${INCL} ${STRICT} pttracks.cc ptformat.cc
	$(CXX) -o ptmute -g ${INCL} ${STRICT} ptmute.cc ptformat.cc
	$(CXX) -o ptrename -g ${INCL} ${STRICT} ptrename.cc ptformat.cc
	$(CXX) -o ptgain -g ${INCL} ${STRICT} ptgain.cc ptformat.cc
	$(CXX) -o ptmove -g ${INCL} ${STRICT} ptmove.cc ptformat.cc
	$(CXX) -o ptsplit -g ${INCL} ${STRICT} ptsplit.cc ptformat.cc
	$(CXX) -o ptfade -g ${INCL} ${STRICT} ptfade.cc ptformat.cc
	$(CXX) -o ptlevel -g ${INCL} ${STRICT} ptlevel.cc ptformat.cc
	$(CXX) -o ptmarker -g ${INCL} ${STRICT} ptmarker.cc ptformat.cc
	$(CXX) -o ptrenametrack -g ${INCL} ${STRICT} ptrenametrack.cc ptformat.cc
	$(CXX) -o ptroute -g ${INCL} ${STRICT} ptroute.cc ptformat.cc

all32:
	$(CXX) -m32 -o ptftool -g ${INCL32} ${STRICT} ptftool.cc ptformat.cc
	$(CXX) -m32 -o ptunxor -g ${INCL32} ${STRICT} ptunxor.cc ptformat.cc
	$(CXX) -m32 -o ptgenmissing -g ${INCL32} ${STRICT} ptgenmissing.cc ptformat.cc
	$(CXX) -m32 -o ptcheckmedia -g ${INCL32} ${STRICT} ptcheckmedia.cc ptformat.cc
	$(CXX) -m32 -o ptdiff -g ${INCL32} ${STRICT} ptdiff.cc ptformat.cc
	$(CXX) -m32 -o pttracks -g ${INCL32} ${STRICT} pttracks.cc ptformat.cc
	$(CXX) -m32 -o ptmute -g ${INCL32} ${STRICT} ptmute.cc ptformat.cc
	$(CXX) -m32 -o ptrename -g ${INCL32} ${STRICT} ptrename.cc ptformat.cc
	$(CXX) -m32 -o ptgain -g ${INCL32} ${STRICT} ptgain.cc ptformat.cc
	$(CXX) -m32 -o ptmove -g ${INCL32} ${STRICT} ptmove.cc ptformat.cc
	$(CXX) -m32 -o ptsplit -g ${INCL32} ${STRICT} ptsplit.cc ptformat.cc
	$(CXX) -m32 -o ptfade -g ${INCL32} ${STRICT} ptfade.cc ptformat.cc
	$(CXX) -m32 -o ptlevel -g ${INCL32} ${STRICT} ptlevel.cc ptformat.cc
	$(CXX) -m32 -o ptmarker -g ${INCL32} ${STRICT} ptmarker.cc ptformat.cc
	$(CXX) -m32 -o ptrenametrack -g ${INCL32} ${STRICT} ptrenametrack.cc ptformat.cc
	$(CXX) -m32 -o ptroute -g ${INCL32} ${STRICT} ptroute.cc ptformat.cc

clangall:
	clang++ -o ptftool -g ${INCL} ${CLANGSTRICT} ptftool.cc ptformat.cc
	clang++ -o ptunxor -g ${INCL} ${CLANGSTRICT} ptunxor.cc ptformat.cc
	clang++ -o ptgenmissing -g ${INCL} ${CLANGSTRICT} ptgenmissing.cc ptformat.cc
	clang++ -o ptcheckmedia -g ${INCL} ${CLANGSTRICT} ptcheckmedia.cc ptformat.cc
	clang++ -o ptdiff -g ${INCL} ${CLANGSTRICT} ptdiff.cc ptformat.cc
	clang++ -o pttracks -g ${INCL} ${CLANGSTRICT} pttracks.cc ptformat.cc
	clang++ -o ptmute -g ${INCL} ${CLANGSTRICT} ptmute.cc ptformat.cc
	clang++ -o ptrename -g ${INCL} ${CLANGSTRICT} ptrename.cc ptformat.cc
	clang++ -o ptgain -g ${INCL} ${CLANGSTRICT} ptgain.cc ptformat.cc
	clang++ -o ptmove -g ${INCL} ${CLANGSTRICT} ptmove.cc ptformat.cc
	clang++ -o ptsplit -g ${INCL} ${CLANGSTRICT} ptsplit.cc ptformat.cc
	clang++ -o ptfade -g ${INCL} ${CLANGSTRICT} ptfade.cc ptformat.cc
	clang++ -o ptlevel -g ${INCL} ${CLANGSTRICT} ptlevel.cc ptformat.cc
	clang++ -o ptmarker -g ${INCL} ${CLANGSTRICT} ptmarker.cc ptformat.cc
	clang++ -o ptrenametrack -g ${INCL} ${CLANGSTRICT} ptrenametrack.cc ptformat.cc
	clang++ -o ptroute -g ${INCL} ${CLANGSTRICT} ptroute.cc ptformat.cc

clean:
	rm ptftool ptunxor ptgenmissing ptcheckmedia ptdiff pttracks ptmute ptrename ptgain ptmove ptsplit ptfade ptlevel ptmarker ptrenametrack ptroute
