"""Setup part `metahuman`: all fighter clips on the MetaHuman skeleton (fourfold.animation.metahuman).

Skipped (status ok, with a note) when no MetaHuman was copied in (Tools/gasp/migrate_metahuman.py).
"""
from .animation import metahuman as _mh


def build_all(force=False):
    return _mh.build(force=force)
