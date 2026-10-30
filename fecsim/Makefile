CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra

.PHONY: all test clean
all: fecsim

fecsim: fecsim.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

# Hand-computed checks: 64 KB message (16 packets of 4 KB), 100 Gbps, RTT 5 µs, RTO 100 µs.
# t_pkt = 0.32768 µs, one-way delay 2.5 µs. Expected p50 completion times are derived in README.md.
test: fecsim
	@./check.sh

clean:
	rm -f fecsim
