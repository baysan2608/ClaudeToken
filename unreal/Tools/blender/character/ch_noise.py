"""Deterministic numpy noise (gradient noise 2D / 3D, fbm, ridged, cellular) for the procedural textures."""
import numpy as np

_RNG = np.random.default_rng(20261006)
_PERM = np.concatenate([_RNG.permutation(256)] * 2).astype(np.int64)
_G2 = np.array([[np.cos(a), np.sin(a)] for a in np.linspace(0, 2 * np.pi, 16, endpoint=False)])
_G3 = np.array([[1, 1, 0], [-1, 1, 0], [1, -1, 0], [-1, -1, 0], [1, 0, 1], [-1, 0, 1], [1, 0, -1], [-1, 0, -1],
                [0, 1, 1], [0, -1, 1], [0, 1, -1], [0, -1, -1], [1, 1, 0], [-1, 1, 0], [0, -1, 1], [0, -1, -1]], float)


def _fade(t):
    return t * t * t * (t * (t * 6 - 15) + 10)


def noise2(x, y, seed=0):
    """Gradient noise in [-1, 1] (approx), arbitrary-shape arrays."""
    x = np.asarray(x, float) + seed * 17.13
    y = np.asarray(y, float) + seed * 31.71
    xi = np.floor(x).astype(np.int64)
    yi = np.floor(y).astype(np.int64)
    xf, yf = x - xi, y - yi
    xi &= 255
    yi &= 255

    def g(ix, iy, dx, dy):
        h = _PERM[_PERM[ix] + iy] & 15
        return _G2[h, 0] * dx + _G2[h, 1] * dy
    u, v = _fade(xf), _fade(yf)
    n00 = g(xi, yi, xf, yf)
    n10 = g(xi + 1, yi, xf - 1, yf)
    n01 = g(xi, yi + 1, xf, yf - 1)
    n11 = g(xi + 1, yi + 1, xf - 1, yf - 1)
    return 1.414 * ((n00 * (1 - u) + n10 * u) * (1 - v) + (n01 * (1 - u) + n11 * u) * v)


def noise3(x, y, z, seed=0):
    x = np.asarray(x, float) + seed * 17.13
    y = np.asarray(y, float) + seed * 31.71
    z = np.asarray(z, float) + seed * 47.31
    xi, yi, zi = (np.floor(a).astype(np.int64) for a in (x, y, z))
    xf, yf, zf = x - xi, y - yi, z - zi
    xi &= 255
    yi &= 255
    zi &= 255

    def g(ix, iy, iz, dx, dy, dz):
        h = _PERM[_PERM[_PERM[ix] + iy] + iz] & 15
        return _G3[h, 0] * dx + _G3[h, 1] * dy + _G3[h, 2] * dz
    u, v, w = _fade(xf), _fade(yf), _fade(zf)
    out = 0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                wgt = (u if dx else 1 - u) * (v if dy else 1 - v) * (w if dz else 1 - w)
                out = out + wgt * g(xi + dx, yi + dy, zi + dz, xf - dx, yf - dy, zf - dz)
    return out


def fbm2(x, y, octaves=4, lac=2.0, gain=0.5, seed=0):
    a, f, s, norm = 1.0, 1.0, 0.0, 0.0
    for o in range(octaves):
        s = s + a * noise2(x * f, y * f, seed + o * 7)
        norm += a
        a *= gain
        f *= lac
    return s / norm


def fbm3(p, scale, octaves=4, seed=0):
    x, y, z = p[..., 0] * scale, p[..., 1] * scale, p[..., 2] * scale
    a, f, s, norm = 1.0, 1.0, 0.0, 0.0
    for o in range(octaves):
        s = s + a * noise3(x * f, y * f, z * f, seed + o * 5)
        norm += a
        a *= 0.5
        f *= 2.0
    return s / norm


def cellular2(x, y, seed=0):
    """F1 distance of a jittered grid (0 at feature points, ~0.5-0.7 between)."""
    x = np.asarray(x, float)
    y = np.asarray(y, float)
    xi = np.floor(x).astype(np.int64)
    yi = np.floor(y).astype(np.int64)
    best = np.full(x.shape, 9.0)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            cx, cy = xi + dx, yi + dy
            h = _PERM[(_PERM[(cx + seed) & 255] + cy) & 255]
            jx = (h & 15) / 15.0
            jy = ((h >> 4) & 15) / 15.0
            d = (cx + jx - x) ** 2 + (cy + jy - y) ** 2
            best = np.minimum(best, d)
    return np.sqrt(best)


def hash01(ix, seed=0):
    """Deterministic pseudo-random [0,1) per integer id."""
    ix = np.asarray(ix, np.int64)
    h = _PERM[(_PERM[(ix + seed) & 255] + (ix >> 8)) & 255]
    h2 = _PERM[(h + (ix >> 3) + seed * 7) & 255]
    return ((h * 256 + h2) % 65536) / 65536.0
