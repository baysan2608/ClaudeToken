"""Clip catalogue (MARTIAL_ARTS.md §3).  Each module registers builder functions with @clip; build_bases() fills the
base stances first.  CATALOG: name -> builder() returning an ffa_dsl.Clip."""
CATALOG = {}
_LOADED = []


def clip(name):
    def deco(fn):
        CATALOG[name] = fn
        return fn
    return deco


def load():
    if _LOADED:
        return CATALOG
    from . import bases
    bases.build_bases()
    bases.build_bases_2()
    from . import shared, reactions, earth, water, fire, air, p2, hand_shapes  # noqa: F401
    _LOADED.append(True)
    return CATALOG
