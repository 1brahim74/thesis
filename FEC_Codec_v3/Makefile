# FEC Codec v3 — Makefile
# Usage: make          → build everything
#        make test     → run all 252 tests
#        make bench    → run benchmark
#        make clean    → remove binaries

CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
FAST     = -std=c++17 -O3 -march=native

.PHONY: all test bench clean

all: fec_bench_v3 test_suite test_suite_ext test_pressure

fec_bench_v3: main.cpp gf256.hpp gf256_matrix.hpp rs_codec.hpp channel.hpp
	$(CXX) $(FAST) -o $@ main.cpp

test_suite: test_suite.cpp gf256.hpp gf256_matrix.hpp rs_codec.hpp channel.hpp
	$(CXX) $(CXXFLAGS) -o $@ test_suite.cpp

test_suite_ext: test_suite_extended.cpp gf256.hpp gf256_matrix.hpp rs_codec.hpp channel.hpp
	$(CXX) $(CXXFLAGS) -o $@ test_suite_extended.cpp

test_pressure: test_suite_pressure.cpp gf256.hpp gf256_matrix.hpp rs_codec.hpp channel.hpp
	$(CXX) $(CXXFLAGS) -o $@ test_suite_pressure.cpp

test: test_suite test_suite_ext test_pressure
	@echo "\n=== Suite 1 ===" && ./test_suite
	@echo "\n=== Suite 2 ===" && ./test_suite_ext
	@echo "\n=== Suite 3 ===" && ./test_pressure

bench: fec_bench_v3
	./fec_bench_v3

clean:
	rm -f fec_bench_v3 test_suite test_suite_ext test_pressure
