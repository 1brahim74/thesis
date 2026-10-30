# Reed–Solomon erasure coding for datacenter RDMA

Code and data for the thesis *Algebraic Coding for Robust High-Speed Data Transfers* and the paper
*When Does Erasure Coding Beat Retransmission for RDMA? Decode Cost Decides at Microsecond RTTs*
(Ibrahim Mammadov, ZJU-UIUC Institute, Zhejiang University).

| Directory | Contents |
|---|---|
| `FEC_Codec_v3/` | C++17 GF(2^8) systematic Cauchy Reed–Solomon codec with m×m erasure-count decoding, channel models (Bernoulli, Gilbert–Elliott, DC3) and tests |
| `bench_isal/` | Decode-latency and throughput benchmark against Intel ISA-L and cm256 (AVX2 / AVX-512 / GFNI), with results from an AWS c7i server and a laptop |
| `fecsim/` | Discrete-event simulator comparing go-back-N, selective repeat, selective repeat with tail-loss probe, and FEC using measured decode costs |
| `paper/` | Paper source (`make` builds `main.pdf`) |

Each directory has its own README with build and run instructions.

## Quick start

```bash
cd bench_isal && make test && ./run_all.sh     # needs ISA-L (see bench_isal/README.md)
cd ../fecsim && make test && ./run_sweeps.sh
cd ../paper && make
```

## License

MIT, see `LICENSE`. ISA-L (BSD-3-Clause) and cm256 (BSD-3-Clause) are downloaded at build time and keep their own licenses.
