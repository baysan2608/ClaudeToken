"""Fourfold animation toolkit - small rotation / vector helpers (numpy only, no bpy).

Conventions
-----------
* Blender armature space (the frozen rig, ff_rig_spec): metres, Z up, the character FACES -Y, character LEFT = +X.
* Authoring frame "A" used by every clip: x = character left, y = character forward, z = up.  A(x, y, z) converts an
  A-frame vector to Blender armature space (x, -y, z).
* Rotations are 3x3 numpy matrices acting on column vectors.  Quaternions are (w, x, y, z) like Blender.
* Body-space Euler triples are (pitch, side, yaw) in degrees: pitch + = top of the segment tips forward, side + = top
  tips toward the character's left, yaw + = the front turns toward the character's left.
"""
import math

import numpy as np

DEG = math.pi / 180.0


def A(x, y, z):
    """A-frame (left, forward, up) -> Blender armature space."""
    return np.array([x, -y, z], dtype=float)


def toA(v):
    """Blender armature space -> A-frame (left, forward, up)."""
    return np.array([v[0], -v[1], v[2]], dtype=float)


def norm(v):
    v = np.asarray(v, dtype=float)
    n = float(np.linalg.norm(v))
    return v / n if n > 1e-12 else v * 0.0


def perp(v, axis):
    """Component of v perpendicular to the unit vector axis."""
    return v - axis * float(np.dot(v, axis))


def rot(axis, deg):
    """Rotation matrix about an arbitrary axis (Rodrigues)."""
    a = norm(axis)
    t = deg * DEG
    c, s = math.cos(t), math.sin(t)
    x, y, z = a
    C = 1.0 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


def rx(deg):
    return rot((1.0, 0.0, 0.0), deg)


def ry(deg):
    return rot((0.0, 1.0, 0.0), deg)


def rz(deg):
    return rot((0.0, 0.0, 1.0), deg)


def body_rot(pitch, side, yaw):
    """Body-space Euler (degrees) -> Blender rotation.  Applied as yaw (about up), then side bend, then pitch
    (intrinsic: pitch first in the yawed / bent frame), which keeps 'pitch forward' meaning forward of the yawed body."""
    # pitch forward = +rotation about Blender +X (top moves to -Y); side left = +rotation about Blender +Y
    # (top moves to +X); yaw left = +rotation about +Z (front -Y swings to +X).
    return rz(yaw) @ ry(side) @ rx(pitch)


def mat_to_quat(m):
    """3x3 rotation -> quaternion (w, x, y, z), w >= 0."""
    m = np.asarray(m, dtype=float)
    t = m[0, 0] + m[1, 1] + m[2, 2]
    if t > 0.0:
        s = math.sqrt(t + 1.0) * 2.0
        w = 0.25 * s
        x = (m[2, 1] - m[1, 2]) / s
        y = (m[0, 2] - m[2, 0]) / s
        z = (m[1, 0] - m[0, 1]) / s
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2.0
        w = (m[2, 1] - m[1, 2]) / s
        x = 0.25 * s
        y = (m[0, 1] + m[1, 0]) / s
        z = (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2.0
        w = (m[0, 2] - m[2, 0]) / s
        x = (m[0, 1] + m[1, 0]) / s
        y = 0.25 * s
        z = (m[1, 2] + m[2, 1]) / s
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2.0
        w = (m[1, 0] - m[0, 1]) / s
        x = (m[0, 2] + m[2, 0]) / s
        y = (m[1, 2] + m[2, 1]) / s
        z = 0.25 * s
    q = np.array([w, x, y, z])
    q /= np.linalg.norm(q)
    if q[0] < 0.0:
        q = -q
    return q


def quat_to_mat(q):
    w, x, y, z = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def quat_slerp(a, b, t):
    a = np.asarray(a, float)
    b = np.asarray(b, float)
    d = float(np.dot(a, b))
    if d < 0.0:
        b = -b
        d = -d
    if d > 0.9995:
        q = a + (b - a) * t
        return q / np.linalg.norm(q)
    th = math.acos(min(1.0, d))
    s = math.sin(th)
    return (math.sin((1 - t) * th) * a + math.sin(t * th) * b) / s


def mat_slerp(a, b, t):
    return quat_to_mat(quat_slerp(mat_to_quat(a), mat_to_quat(b), t))


def rot_between(u, v):
    """Minimal rotation taking unit vector u to unit vector v."""
    u = norm(u)
    v = norm(v)
    c = float(np.dot(u, v))
    if c > 1.0 - 1e-10:
        return np.eye(3)
    if c < -1.0 + 1e-10:
        ax = np.cross(u, (1.0, 0.0, 0.0))
        if np.linalg.norm(ax) < 1e-6:
            ax = np.cross(u, (0.0, 1.0, 0.0))
        return rot(ax, 180.0)
    ax = np.cross(u, v)
    return rot(ax, math.degrees(math.atan2(np.linalg.norm(ax), c)))


def angle_of(m):
    """Rotation angle of a rotation matrix (degrees, 0..180)."""
    c = (float(np.trace(m)) - 1.0) * 0.5
    return math.degrees(math.acos(max(-1.0, min(1.0, c))))


def swing_twist_y(q):
    """Split a LOCAL rotation (quaternion w,x,y,z) into twist about local +Y and the swing: returns (twist_deg,
    swing_quat).  q = swing * twist."""
    w, x, y, z = q
    n = math.hypot(w, y)
    if n < 1e-9:
        tw = np.array([1.0, 0.0, 0.0, 0.0])
        twist_deg = 180.0
    else:
        tw = np.array([w / n, 0.0, y / n, 0.0])
        twist_deg = math.degrees(2.0 * math.atan2(tw[2], tw[0]))
    # swing = q * conj(twist)
    sw = quat_mul(q, np.array([tw[0], -tw[1], -tw[2], -tw[3]]))
    if twist_deg > 180.0:
        twist_deg -= 360.0
    elif twist_deg < -180.0:
        twist_deg += 360.0
    return twist_deg, sw


def quat_mul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return np.array([w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
                     w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
                     w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
                     w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2])


def frame_from(y_axis, z_hint):
    """Orthonormal frame [X Y Z] with Y exactly along y_axis and Z as close as possible to z_hint."""
    y = norm(y_axis)
    z = perp(np.asarray(z_hint, float), y)
    if np.linalg.norm(z) < 1e-8:
        z = perp(np.array([0.0, 0.0, 1.0]) if abs(y[2]) < 0.9 else np.array([0.0, -1.0, 0.0]), y)
    z = norm(z)
    x = np.cross(y, z)
    return np.stack([x, y, z], axis=1)


def smoothstep(u):
    u = min(max(u, 0.0), 1.0)
    return u * u * (3.0 - 2.0 * u)


def lerp(a, b, u):
    return a + (b - a) * u
