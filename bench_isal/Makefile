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
# cm256 (BSD-3-Clause) pinned to a known commit; built with -mavx2 so its gf256 uses AVX2
CM256_DIR ?= cm256-src
CM256_REV ?= 634d1e36b095dbba937586a7e9843d828762dae4
CM256_OBJS = $(CM256_DIR)/cm256.o $(CM256_DIR)/gf256.o

ifneq ($(wildcard isal-install/include/isa-l/erasure_code.h),)
  ISAL_CFLAGS = -Iisal-install/include
  ISAL_LIBS   = -Lisal-install/lib -Wl,-rpath,$(CURDIR)/isal-install/lib -lisal
else
  ISAL_CFLAGS =
  ISAL_LIBS   = -lisal
endif

HDRS = gf256_simd.hpp rs_fast.hpp isal_ref.hpp cm256_ref.hpp ../FEC_Codec_v3/gf256.hpp \
       ../FEC_Codec_v3/gf256_matrix.hpp ../FEC_Codec_v3/rs_codec.hpp

.PHONY: all test isal clean distclean

all: test_simd bench_isal

$(CM256_DIR)/include/cm256.h:
	rm -rf $(CM256_DIR)
	git clone -q https://github.com/catid/cm256.git $(CM256_DIR)
	cd $(CM256_DIR) && git checkout -q $(CM256_REV)

$(CM256_DIR)/%.o: $(CM256_DIR)/include/cm256.h
	$(CXX) -std=c++17 -O3 -mavx2 -w -I$(CM256_DIR)/include -c $(CM256_DIR)/src/$*.cpp -o $@

test_simd: test_simd.cpp $(HDRS) $(CM256_OBJS)
	$(CXX) $(CXXFLAGS) $(ISAL_CFLAGS) -I$(CM256_DIR)/include -o $@ $< $(CM256_OBJS) $(ISAL_LIBS)

bench_isal: bench_isal.cpp $(HDRS) $(CM256_OBJS)
	$(CXX) $(CXXFLAGS) $(ISAL_CFLAGS) -I$(CM256_DIR)/include -o $@ $< $(CM256_OBJS) $(ISAL_LIBS) -lpthread

test: test_simd
	./test_simd

# Needs: git, nasm (>= 2.14), autoconf, automake, libtool
isal:
	rm -rf isal-src isal-install
	git clone --depth 1 --branch $(ISAL_TAG) https://github.com/intel/isa-l.git isal-src
	cd isal-src && ./autogen.sh && ./configure --prefix=$(CURDIR)/isal-install && $(MAKE) -j && $(MAKE) install

clean:
	rm -f test_simd bench_isal $(CM256_OBJS)

distclean: clean
	rm -rf $(CM256_DIR) isal-src isal-install
