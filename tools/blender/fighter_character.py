"""Assemble the fighter mesh from the body / garment / hand modules."""
from fighter_mesh import MeshBuilder

import fighter_body as body


def build_character():
    mb = MeshBuilder()
    body.build_head(mb)
    body.build_neck(mb)
    body.build_hair(mb)
    body.build_torso_skin(mb)
    try:
        import fighter_garments as garm
        garm.build_garments(mb)
    except ImportError:
        pass
    try:
        import fighter_hands as hands
        hands.build_arms_and_hands(mb)
    except ImportError:
        pass
    mb.pack_uvs(log=print)
    return mb
