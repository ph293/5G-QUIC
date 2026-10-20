#!/bin/bash
# Runs 7 modes x 3 flood shapes x 2 control delays x 10 seeds = 420 runs.
# Usage (from the ns-3 folder):  bash scratch/sdnmec_v2/run_all.sh
OUT=results_mm.csv
rm -f "$OUT"
./ns3 build || exit 1
for sh in step ramp periodic; do
  for dl in 0 2; do
    for m in none pid ff mmraw mm mmsnn mmsnnu; do
      for r in 1 2 3 4 5 6 7 8 9 10; do
        echo "shape=$sh delay=$dl mode=$m run=$r"
        ./ns3 run --no-build "main-compare --mode=$m --run=$r --shape=$sh --delay=$dl --burstInterval=0.01 --summary=false --out=$OUT" > /dev/null 2>&1
      done
    done
  done
done
echo "Finished. Results are in $OUT"
