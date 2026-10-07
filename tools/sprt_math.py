"""Pentanomial SPRT maths, ported line by line from fastchess (app/src/matchmaking/sprt/sprt.cpp, commit a281caf),
so results merged from many cloud jobs give exactly the LLR fastchess would print for the same games.
Pentanomial order everywhere: [LL, LD, DD+WL, WD, WW] (fastchess's "Ptnml(0-2)" line)."""
import math

SCORES = [0.0, 0.25, 0.5, 0.75, 1.0]
NELO_SCALE = 800.0 / math.log(10)


def bounds(alpha=0.05, beta=0.05):
    return math.log(beta / (1 - alpha)), math.log((1 - beta) / alpha)


def _itp(f, a, b, f_a, f_b, k_1, k_2, n_0, eps):
    if f_a > 0:
        a, b, f_a, f_b = b, a, f_b, f_a
    n_half = math.ceil(math.log2(abs(b - a) / (2.0 * eps)))
    n_max = n_half + n_0
    i = 0
    while abs(b - a) > 2.0 * eps:
        x_half = (a + b) / 2.0
        r = eps * 2.0 ** (n_max - i) - (b - a) / 2.0
        delta = k_1 * abs(b - a) ** k_2  # fastchess: pow(b - a, k_2) with k_2 = 2, same value
        x_f = (f_b * a - f_a * b) / (f_b - f_a)
        sigma = (x_half - x_f) / abs(x_half - x_f)
        x_t = x_f + sigma * delta if delta <= abs(x_half - x_f) else x_half
        x_itp = x_t if abs(x_t - x_half) <= r else x_half - sigma * r
        f_itp = f(x_itp)
        if f_itp == 0:
            a = b = x_itp
        elif f_itp < 0:
            a, f_a = x_itp, f_itp
        else:
            b, f_b = x_itp, f_itp
        i += 1
    return (a + b) / 2.0


def _mean_var(p):
    mu = sum(x * q for x, q in zip(SCORES, p))
    return mu, sum(q * (x - mu) ** 2 for x, q in zip(SCORES, p))


def _mle(probs, mu_ref, t_star):
    p = [1.0 / len(SCORES)] * len(SCORES)
    for _ in range(10):
        mu, var = _mean_var(p)
        sigma = math.sqrt(var)
        phi = [a - mu_ref - 0.5 * t_star * sigma * (1.0 + ((a - mu) / sigma) ** 2) for a in SCORES]
        u, v = min(phi), max(phi)
        theta = _itp(lambda x: sum(ph * f / (1.0 + x * f) for ph, f in zip(probs, phi)),
                     -1.0 / v, -1.0 / u, math.inf, -math.inf, 0.1, 2.0, 0.99, 1e-7)
        newp = [ph / (1.0 + theta * f) for ph, f in zip(probs, phi)]
        max_diff = max(abs(n - o) for n, o in zip(newp, p))
        p = newp
        if max_diff < 1e-4:
            break
    return p


def llr(penta, elo0, elo1):
    """Normalized-Elo pentanomial LLR (fastchess model=normalized)."""
    counts = [c if c else 1e-3 for c in penta]
    total = sum(counts)
    probs = [c / total for c in counts]
    t0 = math.sqrt(2.0) * elo0 / NELO_SCALE
    t1 = math.sqrt(2.0) * elo1 / NELO_SCALE
    p0, p1 = _mle(probs, 0.5, t0), _mle(probs, 0.5, t1)
    return total * sum(ph * (math.log(b) - math.log(a)) for ph, a, b in zip(probs, p0, p1))


def elo(penta):
    """Logistic Elo and normalized Elo with 95 % intervals from pentanomial counts."""
    n = sum(penta)
    probs = [c / n for c in penta]
    mu, var = _mean_var(probs)
    se = math.sqrt(var / n)
    to_elo = lambda s: -400.0 * math.log10(1.0 / min(max(s, 1e-9), 1 - 1e-9) - 1.0)
    e, lo, hi = to_elo(mu), to_elo(mu - 1.959964 * se), to_elo(mu + 1.959964 * se)
    nelo = (mu - 0.5) / math.sqrt(2.0 * var) * NELO_SCALE if var > 0 else 0.0
    return e, (hi - lo) / 2.0, nelo


if __name__ == "__main__":  # check against LLRs fastchess printed for our own runs (rounded to 2 decimals)
    for name, penta, e0, e1, want in [
        ("rfp", [4, 24, 88, 122, 41], 0, 5, 2.95),
        ("asp", [0, 45, 132, 126, 26], 0, 5, 2.96),
        ("lmp", [17, 182, 304, 159, 18], 0, 5, -0.65),
        ("nnue", [0, 0, 3, 6, 68], 0, 10, 2.95),
    ]:
        got = llr(penta, e0, e1)
        assert abs(got - want) < 0.006, (name, got, want)
        print(f"{name}: LLR {got:.3f} (fastchess {want}) Elo {elo(penta)[0]:+.1f} ± {elo(penta)[1]:.1f}")
    print("sprt_math OK")
