# bench_isal — v3 m×m decoder + SIMD vs Intel ISA-L
#
#   make            build test_simd and bench_isal
#   make test       run correctness tests (must pass before any benchmark)
#   make isal       build ISA-L v2.31.0 from source into ./isal-install (if no system libisal)
#
# ISA-L location: system install (apt install libisal-dev) is used by default.
# If ./isal-install exists (from `make isal`), it takes precedence.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra
ISAL_TAG ?= v2.31.0

ifneq ($(wildcard isal-install/include/isa-l/erasure_code.h),)
  ISAL_CFLAGS = -Iisal-install/include
  ISAL_LIBS   = -Lisal-install/lib -Wl,-rpath,$(CURDIR)/isal-install/lib -lisal
else
  ISAL_CFLAGS =
  ISAL_LIBS   = -lisal
endif

HDRS = gf256_simd.hpp rs_fast.hpp isal_ref.hpp ../FEC_Codec_v3/gf256.hpp \
       ../FEC_Codec_v3/gf256_matrix.hpp ../FEC_Codec_v3/rs_codec.hpp

.PHONY: all test isal clean

all: test_simd bench_isal

test_simd: test_simd.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) $(ISAL_CFLAGS) -o $@ $< $(ISAL_LIBS)

bench_isal: bench_isal.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) $(ISAL_CFLAGS) -o $@ $< $(ISAL_LIBS) -lpthread

test: test_simd
	./test_simd

# Needs: git, nasm (>= 2.14), autoconf, automake, libtool
isal:
	rm -rf isal-src isal-install
	git clone --depth 1 --branch $(ISAL_TAG) https://github.com/intel/isa-l.git isal-src
	cd isal-src && ./autogen.sh && ./configure --prefix=$(CURDIR)/isal-install && $(MAKE) -j && $(MAKE) install

clean:
	rm -f test_simd bench_isal
