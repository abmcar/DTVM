import json, sys, math, random
from statistics import mean, pstdev

def load_per_bench(path):
    data = json.load(open(path))
    out = {}
    for b in data["benchmarks"]:
        name = b["name"].rsplit("_mean", 1)[0].rsplit("_median", 1)[0]
        if b["name"].endswith("_mean"):
            out.setdefault(name, {})["mean"] = b["real_time"]
        elif b["name"].endswith("_stddev"):
            out.setdefault(name, {})["stddev"] = b["real_time"]
    return out

def bootstrap_geomean(ratios, B=10000, seed=42):
    rng = random.Random(seed)
    gms = []
    n = len(ratios)
    for _ in range(B):
        sample = [ratios[rng.randrange(n)] for _ in range(n)]
        gms.append(math.exp(sum(math.log(r) for r in sample) / n))
    gms.sort()
    return gms[int(0.025*B)], gms[int(0.5*B)], gms[int(0.975*B)]

def main():
    w = load_per_bench(sys.argv[1])
    wo = load_per_bench(sys.argv[2])
    print("benchmark,with_mean,with_stddev,without_mean,without_stddev,speedup_pct,cv_with,cv_without")
    ratios = []
    for name in sorted(w.keys() & wo.keys()):
        wm, ws = w[name]["mean"], w[name].get("stddev", 0.0)
        om, os_ = wo[name]["mean"], wo[name].get("stddev", 0.0)
        speedup = (om - wm) / om * 100.0
        cv_w = (ws / wm * 100) if wm else 0.0
        cv_wo = (os_ / om * 100) if om else 0.0
        ratios.append(om / wm)
        print(f"{name},{wm:.3f},{ws:.3f},{om:.3f},{os_:.3f},{speedup:.3f},{cv_w:.2f},{cv_wo:.2f}")
    lo, med, hi = bootstrap_geomean(ratios)
    print(f"# geomean_speedup_pct={(med-1)*100:.3f} ci95_low={(lo-1)*100:.3f} ci95_high={(hi-1)*100:.3f} n={len(ratios)}", file=sys.stderr)

if __name__ == "__main__":
    main()
