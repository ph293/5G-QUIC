#!/bin/bash
# Generalisation test: flood speeds and shapes the LRSNN was NOT trained on.
# 6 cases x 2 control delays x 5 modes x 10 seeds = 600 runs.
# Usage (from the ns-3 folder):  bash scratch/sdnmec_v2/run_ood.sh
OUT=results_ood.csv
rm -f "$OUT"
./ns3 build || exit 1
run_case () {   # shape burstInterval
  for dl in 0 2; do
    for m in none pid mmraw mm mmsnn; do
      for r in 1 2 3 4 5 6 7 8 9 10; do
        echo "shape=$1 bi=$2 delay=$dl mode=$m run=$r"
        ./ns3 run --no-build "main-compare --mode=$m --run=$r --shape=$1 --delay=$dl --burstInterval=$2 --summary=false --out=$OUT" > /dev/null 2>&1
      done
    done
  done
}
run_case step 0.02
run_case step 0.007
run_case step 0.0035
run_case spike 0.01
run_case twin 0.01
run_case saw 0.01
echo "Finished. Results are in $OUT"
