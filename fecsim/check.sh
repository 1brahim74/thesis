#!/usr/bin/env bash
# Hand-computed protocol checks (see README.md "Validation"). Exit 1 on any mismatch.
set -u
cd "$(dirname "$0")"
fail=0
# args: description, fecsim args, proto, expected p50 (µs)
check() {
  got=$(./fecsim $2 --decode zero --trials 1 | awk -F, -v p="$3" 'NR>1 && $1==p {print $18}')
  if awk -v g="$got" -v e="$4" 'BEGIN{exit !(g-e<0.002 && e-g<0.002)}'; then
    printf "ok    %-48s %-4s %8s\n" "$1" "$3" "$got"
  else
    printf "FAIL  %-48s %-4s got %s expected %s\n" "$1" "$3" "$got" "$4"; fail=1
  fi
}
B="--sizes 64K --rtts 5 --codes 18:16"
check "no loss"                          "$B --force 9999"   gbn 7.743
check "no loss"                          "$B --force 9999"   sr  7.743
check "no loss"                          "$B --force 9999"   fec 7.743
check "one mid loss (tx 5)"              "$B --force 5"      gbn 13.398
check "one mid loss (tx 5)"              "$B --force 5"      sr  10.121
check "one mid loss (tx 5)"              "$B --force 5"      fec 8.071
check "tail loss (tx 15) -> RTO"         "$B --force 15"     gbn 112.743
check "tail loss (tx 15) -> RTO"         "$B --force 15"     sr  112.743
check "tail loss (tx 15), parity covers" "$B --force 15"     fec 8.071
check "3 losses > parity, NAK at cw end" "$B --force 5,6,7"  sr  11.432
check "3 losses > parity, NAK at cw end" "$B --force 5,6,7"  fec 13.726
check "... and last parity lost -> RTO"  "$B --force 5,6,7,17" fec 113.726
check "tail loss, probe after 2xRTT"     "$B --force 15"     srtlp 18.071
check "mid loss, no spurious probe"      "$B --force 5"      srtlp 10.121
check "retransmission lost again"        "$B --force 15,16"  sr  212.743
check "2 codewords, 3 lost in first"     "--sizes 128K --rtts 5 --codes 18:16 --force 2,3,4" fec 13.969
check "2 codewords, 3 lost in first"     "--sizes 128K --rtts 5 --codes 18:16 --force 2,3,4" sr  13.969
exit $fail
