# LRSNN: Lagrangian-relaxed spiking neural network forecaster (offline training).
# Fixed random LIF reservoir + linear readout trained by Lagrangian-relaxed least squares.
import numpy as np, json, sys
import os
N, T = 40, 10
WQ, WP = float(os.environ.get('WQ', 0.2)), float(os.environ.get('WP', 1.0))
HS = [1, 2, 3]
rng = np.random.default_rng(2026)

# ---------------- reservoir (fixed, never trained) ----------------
beta = rng.uniform(0.6, 0.97, N)
theta = 1.0
s_i = rng.choice([-1.0, 1.0], N)
c_i = rng.uniform(0.0, 1.0, N) ** 1.5
K, I0, XSC = 0.45, 0.12, 0.5
DMAX = 3.5
def enc(d): return float(np.sqrt(min(max(d, 0.0), DMAX) / DMAX))
Iw = K * s_i
Ib = I0 - K * s_i * c_i
W = rng.normal(0, 1, (N, N)) * (rng.random((N, N)) < 0.15)
np.fill_diagonal(W, 0)
W *= 0.25 / max(abs(np.linalg.eigvals(W)))

class Snn:
    def __init__(self):
        self.v = np.zeros(N); self.sp = np.zeros(N); self.c1 = np.zeros(N)
    def observe(self, d):
        x = enc(d)
        cur = Iw * x + Ib
        cnt = np.zeros(N)
        for _ in range(T):
            self.v = beta * self.v + cur + W @ self.sp
            s = (self.v >= theta).astype(float)
            self.v = self.v - theta * s
            self.sp = s
            cnt += s
        c0 = cnt / T
        feat = np.concatenate([c0, self.c1, self.v, [1.0]])
        self.c1 = c0
        return feat

# ---------------- synthetic traces of per-(MEC,slice) offered demand ----------------
def trace(rs, kind, bi, start, end, per, base_int, size, nwin=25):
    svc = 0.010 + 1e-5 * size
    t = rs.uniform(0, base_int); arr = []
    while t < nwin + 5:
        arr.append(t)
        if kind == 'none' or not (start <= t < end): iv = base_int
        elif kind == 'step': iv = bi
        elif kind == 'ramp': iv = 0.04 + (bi - 0.04) * (t - start) / (end - start)
        else: iv = bi if int((t - start) / per) % 2 == 0 else base_int
        t += iv * rs.uniform(0.93, 1.07)
    arr = np.array(arr)
    d = np.array([svc * np.sum((arr > k - 1) & (arr <= k)) for k in range(1, nwin + 1)])
    return d   # d[k-1] = demand in window ending at cycle k

def random_trace(rs):
    base = rs.uniform(0.28, 0.65); size = rs.integers(512, 1300)
    if rs.random() < 0.45: return trace(rs, 'none', 0, 0, 0, 2, base, size)
    kind = rs.choice(['step', 'ramp', 'periodic'])
    bi = rs.uniform(0.005, 0.04); st = rs.uniform(3, 8); en = st + rs.uniform(6, 12)
    return trace(rs, kind, bi, st, en, rs.choice([1, 2, 3, 4]), base, size)

def dataset(traces, h):
    X, Y = [], []
    for d in traces:
        m = Snn()
        for k in range(len(d)):
            f = m.observe(d[k])
            if k + h < len(d): X.append(f); Y.append(d[k + h] * XSC)
    return np.array(X), np.array(Y)

def fit(X, Y, lam_eps_ratio=None, gamma=1e-3, iters=12):
    R = 1.0 / (WQ + Y) ** WP
    A = X.T @ (X * R[:, None]) + gamma * np.eye(X.shape[1])
    w0 = np.linalg.solve(A, X.T @ (R * Y))
    if lam_eps_ratio is None: return w0, 0.0, None
    under = lambda w: np.mean(np.maximum(0, Y - X @ w) ** 2)
    eps = lam_eps_ratio * under(w0)
    lam, w = 0.0, w0
    for _ in range(60):                      # dual ascent on the Lagrange multiplier
        for _ in range(iters):               # primal: reweighted least squares for fixed lambda
            r = Y - X @ w
            D = R * (1.0 + lam * (r > 0))
            w = np.linalg.solve(X.T @ (X * D[:, None]) + gamma * np.eye(X.shape[1]), X.T @ (D * Y))
        g = under(w) - eps
        lam = max(0.0, lam + 8.0 * g / max(eps, 1e-12))
        if abs(g) < 1e-3 * eps: break
    return w, lam, (under(w0), under(w), eps)

class Holt:
    def __init__(s, a=0.8, b=0.5): s.a, s.b, s.init = a, b, False; s.l = s.t = 0
    def obs(s, v):
        if not s.init: s.l, s.t, s.init = v, 0, True; return
        nl = s.a * v + (1 - s.a) * (s.l + s.t); s.t = s.b * (nl - s.l) + (1 - s.b) * s.t; s.l = nl
    def fc(s, h): return max(0.0, s.l + h * s.t)

if __name__ == '__main__':
    rs = np.random.default_rng(777)
    train = [random_trace(rs) for _ in range(2500)]
    ro_ridge, ro_lag, info = {}, {}, {}
    for h in HS:
        X, Y = dataset(train, h)
        ro_ridge[h], _, _ = fit(X, Y)
        ro_lag[h], lam, g = fit(X, Y, lam_eps_ratio=0.5)
        info[h] = (lam, g)
        print(f"h={h} samples={len(Y)} lambda*={lam:.3f} under-pred hinge: unconstrained={g[0]:.5f} -> constrained={g[1]:.5f} (limit {g[2]:.5f})")
    # held-out evaluation on traces that mimic the ns-3 test conditions (different seed from training)
    rt = np.random.default_rng(99991)
    tests = {'step': [], 'ramp': [], 'periodic': [], 'baseline': []}
    for _ in range(300):
        base = rt.uniform(0.3, 0.6); size = int(rt.integers(512, 1300))
        tests['step'].append(trace(rt, 'step', 0.01, 5, 15, 2, base, size))
        tests['ramp'].append(trace(rt, 'ramp', 0.01, 5, 15, 2, base, size))
        tests['periodic'].append(trace(rt, 'periodic', 0.01, 5, 15, 2, base, size))
        tests['baseline'].append(trace(rt, 'none', 0, 0, 0, 2, base, size))
    print("\nHeld-out forecast error (demand units, CPU-s/s). RMSE | mean under-prediction")
    print(f"{'shape':9s}{'h':>2s} {'Holt':>16s} {'LRSNN-ridge':>16s} {'LRSNN-Lagr.':>16s}")
    for sh, trs in tests.items():
        for h in (1, 3):
            res = {}
            for name in ('holt', 'ridge', 'lag'):
                e = []
                for d in trs:
                    m = Snn(); H = Holt()
                    for k in range(len(d) - h):
                        f = m.observe(d[k]); H.obs(d[k])
                        p = H.fc(h) if name == 'holt' else (f @ (ro_ridge if name == 'ridge' else ro_lag)[h]) / XSC
                        e.append(d[k + h] - max(0.0, p))
                e = np.array(e)
                res[name] = (np.sqrt(np.mean(e ** 2)), np.mean(np.maximum(0, e)))
            print(f"{sh:9s}{h:2d} " + " ".join(f"{res[n][0]:8.3f}|{res[n][1]:6.3f}" for n in ('holt', 'ridge', 'lag')))
    np.savez('lrsnn_params.npz', beta=beta, Iw=Iw, Ib=Ib, W=W,
             **{f'ridge{h}': ro_ridge[h] for h in HS}, **{f'lag{h}': ro_lag[h] for h in HS})
