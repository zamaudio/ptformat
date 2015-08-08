STRICT=-Wall -Wcast-align -Wextra -Wwrite-strings -Wunsafe-loop-optimizations -Wlogical-op

all:
	$(CXX) -o ptftool -g ${STRICT} ptftool.cc ptfformat.cc
	$(CXX) -o ptunxor -g ${STRICT} ptunxor.cc ptfformat.cc
clean:
	rm ptftool ptunxor
