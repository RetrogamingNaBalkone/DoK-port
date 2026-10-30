"""Read N21P stage snapshots without reinterpreting frozen ABI1 baselines."""
from run_joint_profile_emulator import FIELDS


def decode_profile(values):
    assert len(values) == 48 and values[0] == 0x21504b44
    assert values[6] == 33513982
    names = list(FIELDS)
    if values[2] == 3:
        names[12:16] = ('geometry_build', 'geometry_cull', 'geometry_merge', 'geometry_doors')
    elif values[2] == 2:
        names[12:16] = ('geometry_build', 'geometry_cull', 'geometry_merge', 'geometry_other')
    else:
        assert values[2] == 1, ('Unknown stage profile ABI', values[2])
    return dict(zip(names, values))
