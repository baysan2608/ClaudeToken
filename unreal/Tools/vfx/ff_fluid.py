"""Fourfold VFX - small deterministic 3D smoke / fire solver + volume ray-marcher (numpy / scipy).

Why not Mantaflow: Blender's Mantaflow fails inside the `bpy` Python module used here ("'LevelsetGrid' object has no
attribute 'setConst'" during bake), so the flipbooks are simulated and rendered by this module instead. It is a
classic stable-fluids solver (Stam 1999; Fedkiw, Stam, Jensen 2001 "Visual simulation of smoke"):

  * collocated grid [x, y, z], y up, cell size 1 (grid units), dt in frames;
  * semi-Lagrangian velocity advection, MacCormack (BFECC-style, clamped) scalar advection for crisp detail;
  * buoyancy (temperature up, soot down), vorticity confinement, time-varying curl-noise turbulence;
  * EXACT pressure projection: backward-difference divergence / forward-difference gradient compose to the 7-point
    Laplacian, solved spectrally with DST-II (open sides x, z: p = 0) and DCT-II (closed floor / ceiling y);
  * combustion: fuel burns above an ignition temperature into heat + soot.

The renderer integrates the density front to back along -z (orthographic), with single scattering from a key light
(transmittance accumulated toward the light), a sky ambient term and black-body emission from temperature. Output
channels (linear, premultiplied where noted) feed the packed flipbook format of docs/fx/README.md:
  R = lighting 0..1 (key + ambient, NOT premultiplied), G = normalised optical thickness, B = emission temperature 0..1,
  A = coverage alpha.
"""
from __future__ import annotations

import numpy as np
from scipy import fft as sfft
from scipy import ndimage


def _smooth_noise(shape, sigma, rng):
    n = rng.standard_normal(shape).astype(np.float32)
    n = ndimage.gaussian_filter(n, sigma, mode="wrap")
    n /= max(float(np.abs(n).max()), 1e-6)
    return n


class Smoke3D:
    def __init__(self, nx, ny, nz, seed=1):
        self.n = (nx, ny, nz)
        self.rng = np.random.default_rng(seed)
        self.u = np.zeros((3, nx, ny, nz), np.float32)
        self.dens = np.zeros((nx, ny, nz), np.float32)
        self.temp = np.zeros((nx, ny, nz), np.float32)
        self.fuel = np.zeros((nx, ny, nz), np.float32)
        # parameters (overridden by presets)
        self.buoy_t = 0.10       # upward accel per unit temperature
        self.buoy_d = 0.01       # downward accel per unit soot
        self.vort = 0.25         # vorticity confinement strength
        self.turb = 0.0          # curl-noise force amplitude
        self.turb_scale = 6.0    # noise blob size (cells)
        self.cool = 0.06         # temperature decay per frame (fraction)
        self.dens_decay = 0.004  # soot dissipation per frame (fraction)
        self.vel_decay = 0.0
        self.ignite = 0.25       # fuel burns where temp > ignite
        self.burn_rate = 0.25    # fraction of fuel burnt per frame
        self.burn_heat = 1.6     # temperature per unit fuel
        self.burn_soot = 0.6     # soot per unit fuel
        self.gravity_floor = True
        # eigenvalues of the 7-point Laplacian in the chosen bases
        kx = np.arange(nx); ky = np.arange(ny); kz = np.arange(nz)
        lx = 2.0 * np.cos(np.pi * (kx + 1) / nx) - 2.0      # DST-II (Dirichlet, open)
        ly = 2.0 * np.cos(np.pi * ky / ny) - 2.0            # DCT-II (Neumann, closed)
        lz = 2.0 * np.cos(np.pi * (kz + 1) / nz) - 2.0
        self.lap = (lx[:, None, None] + ly[None, :, None] + lz[None, None, :]).astype(np.float64)
        self.grid = np.stack(np.meshgrid(np.arange(nx, dtype=np.float32), np.arange(ny, dtype=np.float32),
                                         np.arange(nz, dtype=np.float32), indexing="ij"))
        self._noise_a = None
        self._noise_b = None
        self.frame = 0

    # ------------------------------------------------------------------ helpers
    def _sample(self, f, coords, order=1):
        return ndimage.map_coordinates(f, coords, order=order, mode="nearest", prefilter=False).reshape(f.shape)

    def _backtrace(self, dt):
        c = self.grid - dt * self.u
        return c.reshape(3, -1)

    def advect_scalar(self, f, dt):
        """MacCormack with min/max clamp (sharper than plain semi-Lagrangian, stable)."""
        c_back = self._backtrace(dt)
        fwd = self._sample(f, c_back)
        c_fwd = (self.grid + dt * self.u).reshape(3, -1)
        back = self._sample(fwd, c_fwd)
        corr = fwd + 0.5 * (f - back)
        # clamp to the neighbourhood of the departure point
        mx = ndimage.maximum_filter(f, size=3, mode="nearest")
        mn = ndimage.minimum_filter(f, size=3, mode="nearest")
        lo = self._sample(mn, c_back)
        hi = self._sample(mx, c_back)
        return np.clip(corr, lo, hi)

    def advect_velocity(self, dt):
        c = self._backtrace(dt)
        self.u = np.stack([self._sample(self.u[i], c) for i in range(3)])

    @staticmethod
    def _to_faces(c, axis, closed):
        """Cell-centred component -> N+1 face values along `axis` (closed ends = 0, open ends copy the edge)."""
        a = np.moveaxis(c, axis, 0)
        f = np.empty((a.shape[0] + 1,) + a.shape[1:], np.float32)
        f[1:-1] = 0.5 * (a[:-1] + a[1:])
        if closed:
            f[0] = 0.0
            f[-1] = 0.0
        else:
            f[0] = a[0]
            f[-1] = a[-1]
        return f

    @staticmethod
    def _to_cells(f, axis):
        return np.moveaxis(0.5 * (f[:-1] + f[1:]), 0, axis)

    def project(self):
        """Exact MAC projection: centres -> faces, spectral Poisson solve, faces -> centres (no half-cell drift)."""
        fx = self._to_faces(self.u[0], 0, False)
        fy = self._to_faces(self.u[1], 1, True)
        fz = self._to_faces(self.u[2], 2, False)
        div = (np.moveaxis(fx[1:] - fx[:-1], 0, 0).astype(np.float64)
               + np.moveaxis(fy[1:] - fy[:-1], 0, 1) + np.moveaxis(fz[1:] - fz[:-1], 0, 2))
        # spectral solve: DST-II on x, z (open: p = 0 beyond the sides) ; DCT-II on y (closed floor / ceiling)
        d = sfft.dst(div, type=2, axis=0, norm="ortho", workers=4)
        d = sfft.dct(d, type=2, axis=1, norm="ortho", workers=4)
        d = sfft.dst(d, type=2, axis=2, norm="ortho", workers=4)
        d /= self.lap
        p = sfft.idst(d, type=2, axis=2, norm="ortho", workers=4)
        p = sfft.idct(p, type=2, axis=1, norm="ortho", workers=4)
        p = sfft.idst(p, type=2, axis=0, norm="ortho", workers=4).astype(np.float32)
        # face gradients with ghost cells: open ends p_ghost = -p_edge, closed ends untouched (faces stay 0)
        px = np.moveaxis(p, 0, 0)
        gx = np.concatenate([px[:1] * 2.0, px[1:] - px[:-1], -px[-1:] * 2.0], axis=0)
        py = np.moveaxis(p, 1, 0)
        gy = np.concatenate([np.zeros_like(py[:1]), py[1:] - py[:-1], np.zeros_like(py[:1])], axis=0)
        pz = np.moveaxis(p, 2, 0)
        gz = np.concatenate([pz[:1] * 2.0, pz[1:] - pz[:-1], -pz[-1:] * 2.0], axis=0)
        fx -= gx
        fy -= gy
        fz -= gz
        self.u[0] = self._to_cells(fx, 0)
        self.u[1] = self._to_cells(fy, 1)
        self.u[2] = self._to_cells(fz, 2)

    def curl(self, a):
        g = [np.gradient(a[i]) for i in range(3)]   # g[i][j] = d a_i / d x_j
        return np.stack([g[2][1] - g[1][2], g[0][2] - g[2][0], g[1][0] - g[0][1]])

    def vorticity_confinement(self, dt):
        if self.vort <= 0:
            return
        w = self.curl(self.u)
        mag = np.sqrt((w ** 2).sum(0)) + 1e-6
        gm = np.stack(np.gradient(mag))
        gm /= np.sqrt((gm ** 2).sum(0)) + 1e-6
        f = np.cross(gm, w, axis=0)
        self.u += dt * self.vort * f

    def turbulence(self, dt):
        if self.turb <= 0:
            return
        if self._noise_a is None:
            self._noise_a = np.stack([_smooth_noise(self.n, self.turb_scale, self.rng) for _ in range(3)])
            self._noise_b = np.stack([_smooth_noise(self.n, self.turb_scale, self.rng) for _ in range(3)])
            self._noise_t = 0.0
        self._noise_t += dt / 24.0
        if self._noise_t >= 1.0:
            self._noise_a = self._noise_b
            self._noise_b = np.stack([_smooth_noise(self.n, self.turb_scale, self.rng) for _ in range(3)])
            self._noise_t -= 1.0
        k = 0.5 - 0.5 * np.cos(np.pi * self._noise_t)
        a = self._noise_a * (1 - k) + self._noise_b * k
        f = self.curl(a) * self.turb_scale
        # only where there is something to stir
        mask = np.clip((self.dens + self.temp) * 4.0, 0.0, 1.0)
        f = f * mask
        # remove the bulk push (density-weighted mean): turbulence deforms the volume, it must not translate it
        msum = float(mask.sum())
        if msum > 1e-3:
            for i in (0, 2):
                f[i] -= mask * (float(f[i].sum()) / msum)
        self.u += dt * self.turb * f

    def combust(self, dt):
        hot = self.temp > self.ignite
        burn = np.where(hot, self.fuel * min(1.0, self.burn_rate * dt), 0.0).astype(np.float32)
        self.fuel -= burn
        self.temp += burn * self.burn_heat
        self.dens += burn * self.burn_soot

    def step(self, dt=1.0, sources=None):
        if sources:
            sources(self)
        self.combust(dt)
        self.u[1] += dt * (self.buoy_t * self.temp - self.buoy_d * self.dens)
        self.vorticity_confinement(dt)
        self.turbulence(dt)
        if self.vel_decay > 0:
            self.u *= (1.0 - self.vel_decay * dt)
        self.project()
        self.advect_velocity(dt)
        self.project()
        self.dens = np.maximum(self.advect_scalar(self.dens, dt), 0.0)
        self.temp = np.maximum(self.advect_scalar(self.temp, dt), 0.0)
        if self.fuel.any():
            self.fuel = np.maximum(self.advect_scalar(self.fuel, dt), 0.0)
        self.temp *= (1.0 - self.cool * dt)
        self.dens *= (1.0 - self.dens_decay * dt)
        if self.sponge > 0.0:
            # absorbing layer under the closed ceiling (and the open sides) so nothing piles up at the borders
            self.dens *= self._sponge_k
            self.temp *= self._sponge_k
            self.fuel *= self._sponge_k
        self.frame += 1

    @property
    def sponge(self):
        return getattr(self, "_sponge", 0.0)

    @sponge.setter
    def sponge(self, width):
        self._sponge = float(width)
        nx, ny, nz = self.n
        g = self.grid
        dy = np.clip((g[1] - (ny - 1) * (1.0 - width)) / max((ny - 1) * width, 1.0), 0, 1)
        dx = np.clip((np.abs(g[0] - (nx - 1) / 2) - (nx - 1) / 2 * (1.0 - width)) / max((nx - 1) / 2 * width, 1.0), 0, 1)
        dz = np.clip((np.abs(g[2] - (nz - 1) / 2) - (nz - 1) / 2 * (1.0 - width)) / max((nz - 1) / 2 * width, 1.0), 0, 1)
        d = np.maximum(np.maximum(dy, dx), dz)
        self._sponge_k = (1.0 - 0.25 * d * d).astype(np.float32)

    # ------------------------------------------------------------------ sources
    def sphere_mask(self, c, r, soft=1.5):
        g = self.grid
        d = np.sqrt((g[0] - c[0]) ** 2 + (g[1] - c[1]) ** 2 + (g[2] - c[2]) ** 2)
        return np.clip((r - d) / soft + 0.5, 0.0, 1.0).astype(np.float32)

    def disc_mask(self, c, r, h, soft=1.5):
        g = self.grid
        d = np.sqrt((g[0] - c[0]) ** 2 + (g[2] - c[2]) ** 2)
        m = np.clip((r - d) / soft + 0.5, 0.0, 1.0) * np.clip((h - np.abs(g[1] - c[1])) / soft + 0.5, 0.0, 1.0)
        return m.astype(np.float32)

    def radial_velocity(self, c, speed, mask, up=0.0):
        g = self.grid
        d = np.stack([g[0] - c[0], g[1] - c[1], g[2] - c[2]])
        ln = np.sqrt((d ** 2).sum(0)) + 1e-3
        d /= ln
        self.u += speed * d * mask
        self.u[1] += up * mask


def render(sim: Smoke3D, extinction=1.6, light_dir=(-0.45, 1.0, 0.35), ambient=0.42, key=1.0, emit_temp_scale=1.0,
           shadow_ext=None, upscale=1.0, crop=None):
    """Front-to-back orthographic integration along -z. Returns float32 image (H, W, 4): R light, G thickness,
    B temperature, A alpha. Image rows go top (high y) -> bottom."""
    dens = sim.dens
    temp = sim.temp
    if upscale != 1.0:
        dens = ndimage.zoom(dens, upscale, order=1)
        temp = ndimage.zoom(temp, upscale, order=1)
    if crop is not None:
        (x0, x1), (y0, y1) = crop
        nx, ny, nz = dens.shape
        sl = (slice(int(x0 * nx), int(x1 * nx)), slice(int(y0 * ny), int(y1 * ny)), slice(None))
        dens = dens[sl]
        temp = temp[sl]
    sigma = dens * extinction / max(upscale, 1e-6)
    sh_ext = (shadow_ext if shadow_ext is not None else extinction) / max(upscale, 1e-6)
    # light transmittance: march toward the key light by shifting columns (approximate oblique light)
    ld = np.asarray(light_dir, np.float64)
    ld /= np.linalg.norm(ld)
    tau = np.zeros_like(sigma)
    acc = np.zeros(sigma.shape[0:1] + sigma.shape[2:], np.float32)
    ny = sigma.shape[1]
    # sweep from the top down along y, sliding sideways by the light's x / z slope
    sx = ld[0] / max(ld[1], 0.2)
    sz = ld[2] / max(ld[1], 0.2)
    for y in range(ny - 1, -1, -1):
        tau[:, y, :] = acc
        layer = dens[:, y, :] * sh_ext
        acc = ndimage.shift(acc, (-sx, -sz), order=1, mode="nearest") + layer
    t_light = np.exp(-tau)
    # ambient occlusion-ish: local average density above
    occ = ndimage.uniform_filter(dens, size=5, mode="constant") * sh_ext * 2.0
    amb = ambient * np.exp(-occ)
    light = key * t_light + amb
    alpha_vox = 1.0 - np.exp(-sigma)
    # front to back along -z (camera at +z looking toward -z): flip z so index 0 is nearest
    a = alpha_vox[:, :, ::-1]
    li = light[:, :, ::-1]
    te = temp[:, :, ::-1]
    trans = np.cumprod(1.0 - a, axis=2)
    trans_before = np.concatenate([np.ones_like(trans[:, :, :1]), trans[:, :, :-1]], axis=2)
    wgt = trans_before * a
    alpha = wgt.sum(2)
    lit = (wgt * li).sum(2)
    # emission: hot voxels glow regardless of soot (premultiplied by their own visibility, not by their alpha)
    emis_w = trans_before * (1.0 - np.exp(-np.maximum(te, 0.0) * 0.9))
    tmax = (emis_w * np.clip(te * emit_temp_scale, 0.0, 1.0)).sum(2)
    tw = emis_w.sum(2)
    tempo = np.where(tw > 1e-4, tmax / np.maximum(tw, 1e-4), 0.0) * np.clip(tw * 1.4, 0.0, 1.0)
    thick = 1.0 - np.exp(-sigma.sum(2) * 0.35)
    lit_n = np.where(alpha > 1e-4, lit / np.maximum(alpha, 1e-4), 0.0)
    img = np.stack([np.clip(lit_n, 0, 1), np.clip(thick, 0, 1), np.clip(tempo, 0, 1), np.clip(alpha, 0, 1)], -1)
    # [x, y] -> image rows (top = high y), cols = x
    img = np.transpose(img, (1, 0, 2))[::-1]
    return img.astype(np.float32)
