"""Fit the morphed base mesh onto the frozen Fourfold rig (numpy only).

Every vertex is carried by "fit bones" (groups of MakeHuman bones, weighted with the CC0 MakeHuman weights). Each fit
bone maps a SOURCE segment (MakeHuman joints) onto its TARGET segment (ff_rig_spec rest pose): a stretch along the bone
axis (about the source head) followed by a rigid motion. Stretches are blended linearly, rigid motions with dual
quaternions (no volume loss when the elbows are straightened from the base mesh's slightly bent arms).
"""
import numpy as np


def _n(a):
    a = np.asarray(a, dtype=np.float64)
    l = np.linalg.norm(a)
    return a / l if l > 1e-12 else a


def frame(head, tail, ref, ref_is_x=False):
    """Rotation matrix with columns X, Y, Z: Y along head->tail, Z (or X) = ref projected perpendicular to Y."""
    y = _n(np.asarray(tail) - np.asarray(head))
    r = np.asarray(ref, dtype=np.float64)
    r = _n(r - y * (r @ y))
    if ref_is_x:
        x = r
        z = np.cross(x, y)
    else:
        z = r
        x = np.cross(y, z)
    return np.stack([_n(x), y, _n(z)], axis=1)


def mat_to_quat(m):
    """3x3 rotation -> quaternion (w, x, y, z)."""
    t = np.trace(m)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        q = [0.25 * s, (m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [(m[2, 1] - m[1, 2]) / s, 0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 2] - m[2, 0]) / s, (m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s]
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[1, 0] - m[0, 1]) / s, (m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s]
    q = np.array(q)
    return q / np.linalg.norm(q)


def qmul(a, b):
    """Hamilton product, arrays (..., 4)."""
    aw, ax, ay, az = np.moveaxis(a, -1, 0)
    bw, bx, by, bz = np.moveaxis(b, -1, 0)
    return np.stack([aw * bw - ax * bx - ay * by - az * bz,
                     aw * bx + ax * bw + ay * bz - az * by,
                     aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw], axis=-1)


class FitBone:
    def __init__(self, name, src_head, src_tail, src_ref, tgt_head, tgt_tail, tgt_ref, ref_is_x=False,
                 stretch=True):
        self.name = name
        self.hs = np.asarray(src_head, float)
        self.ht = np.asarray(tgt_head, float)
        self.Rs = frame(src_head, src_tail, src_ref, ref_is_x)
        self.Rt = frame(tgt_head, tgt_tail, tgt_ref, ref_is_x)
        ls = np.linalg.norm(np.asarray(src_tail) - self.hs)
        lt = np.linalg.norm(np.asarray(tgt_tail) - self.ht)
        self.k = (lt / ls) if (stretch and ls > 1e-9) else 1.0
        # stretch matrix in world (source) space about hs
        self.S = self.Rs @ np.diag([1.0, self.k, 1.0]) @ self.Rs.T
        # rigid: x -> R (x - hs) + ht
        self.R = self.Rt @ self.Rs.T
        self.t = self.ht - self.R @ self.hs

    @staticmethod
    def identity(name):
        b = FitBone.__new__(FitBone)
        b.name = name
        b.hs = np.zeros(3)
        b.ht = np.zeros(3)
        b.Rs = np.eye(3)
        b.Rt = np.eye(3)
        b.k = 1.0
        b.S = np.eye(3)
        b.R = np.eye(3)
        b.t = np.zeros(3)
        return b

    def dual_quat(self):
        q = mat_to_quat(self.R)
        tq = np.array([0.0, *self.t])
        d = 0.5 * qmul(tq, q)
        return q, d


def deform(verts, weights, bones):
    """verts (n,3); weights (n, nb) rows summing to 1; bones list of FitBone (same order as weight columns)."""
    v = np.asarray(verts, float)
    W = np.asarray(weights, float)
    # 1) blended stretch (linear, in source space)
    v1 = np.zeros_like(v)
    for j, b in enumerate(bones):
        wj = W[:, j]
        m = wj > 0
        if not m.any():
            continue
        v1[m] += wj[m, None] * ((v[m] - b.hs) @ b.S.T + b.hs)
    # 2) dual-quaternion blend of the rigid parts
    qs = np.array([b.dual_quat()[0] for b in bones])
    ds = np.array([b.dual_quat()[1] for b in bones])
    # align signs to the dominant bone per vertex
    dom = np.argmax(W, axis=1)
    q0 = qs[dom]                                      # (n,4)
    dots = q0 @ qs.T                                  # (n, nb)
    sg = np.where(dots < 0, -1.0, 1.0)
    Wq = W * sg
    bq = Wq @ qs                                      # (n,4)
    bd = Wq @ ds
    nrm = np.linalg.norm(bq, axis=1, keepdims=True)
    bq /= nrm
    bd /= nrm
    # rotate v1 by bq, then translation t = 2 * bd * conj(bq)
    w, x, y, z = bq.T
    R = np.empty((len(v), 3, 3))
    R[:, 0, 0] = 1 - 2 * (y * y + z * z)
    R[:, 0, 1] = 2 * (x * y - w * z)
    R[:, 0, 2] = 2 * (x * z + w * y)
    R[:, 1, 0] = 2 * (x * y + w * z)
    R[:, 1, 1] = 1 - 2 * (x * x + z * z)
    R[:, 1, 2] = 2 * (y * z - w * x)
    R[:, 2, 0] = 2 * (x * z - w * y)
    R[:, 2, 1] = 2 * (y * z + w * x)
    R[:, 2, 2] = 1 - 2 * (x * x + y * y)
    conj = bq * np.array([1, -1, -1, -1])
    tq = 2.0 * qmul(bd, conj)
    return np.einsum("vij,vj->vi", R, v1) + tq[:, 1:]
