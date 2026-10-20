# Usage: python3 scratch/sdnmec_v2/summarize.py results_mm.csv
import csv, sys, statistics as st
rows = list(csv.DictReader(open(sys.argv[1] if len(sys.argv) > 1 else "results_mm.csv")))
keys = ["victim_loss_pct", "victim_avg_delay_ms", "victim_p95_delay_ms", "flooder_loss_pct", "mec_overshoot_s"]
for sh in ("step", "ramp", "periodic"):
    for dl in sorted({r["delay"] for r in rows}):
        print(f"\nshape={sh} delay={dl}s")
        print(f"{'mode':6s}" + "".join(f"{k[:16]:>18s}" for k in keys) + "  runs")
        for m in ("none", "pid", "ff", "mmraw", "mm", "mmsnn", "mmsnnu"):
            rs = [r for r in rows if r["shape"] == sh and r["delay"] == dl and r["mode"] == m]
            if not rs: continue
            cells = "".join(f"{st.mean(float(r[k]) for r in rs):>12.2f} ±{st.pstdev(float(r[k]) for r in rs):>3.1f}" for k in keys)
            print(f"{m:6s}{cells}  {len(rs)}")
