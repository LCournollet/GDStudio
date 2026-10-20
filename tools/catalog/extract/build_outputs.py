"""Assemble object_frames.json, object_types.json and deco_candidates.json from the
intermediate static-analysis results (all derived from the installed GeometryDash.exe and
its Resources plists; nothing downloaded).

inputs (cwd): raw_toolbox.json classes.json customsetup_raw.json slot140_raw.json
              frames_uhd.json pixstats.json
"""
import json, re, collections, os, sys

t = {int(k): v for k, v in json.load(open('raw_toolbox.json')).items()}
cls = {int(k): [c for c in v['classes'] if c][0] for k, v in json.load(open('classes.json')).items()}
cinit = {int(k): v['init_type_writes'] for k, v in json.load(open('classes.json')).items()}
cs = {int(k): v for k, v in json.load(open('customsetup_raw.json')).items()}
s140 = {int(k): v for k, v in json.load(open('slot140_raw.json')).items()}
frames = json.load(open('frames_uhd.json'))
pix = json.load(open('pixstats.json'))

EXE_INFO = "GeometryDash.exe (Steam, Windows x64, 13,178,880 bytes, PE build timestamp 2026-10-17 02:12 UTC; no version string in binary) - user reports GD 2.2091"

# ---------------------------------------------------------------- object_frames.json
json.dump({str(k): t[k] for k in sorted(t)}, open('object_frames.json', 'w'), indent=1)

# ---------------------------------------------------------------- helpers
TYPE_NAMES = {0: 'Solid', 2: 'Hazard', 3: 'InverseGravityPortal', 4: 'NormalGravityPortal', 5: 'ShipPortal',
              6: 'CubePortal', 7: 'Decoration', 8: 'YellowJumpPad', 9: 'PinkJumpPad', 10: 'GravityPad',
              11: 'YellowJumpRing', 12: 'PinkJumpRing', 13: 'GravityRing', 14: 'InverseMirrorPortal',
              15: 'NormalMirrorPortal', 16: 'BallPortal', 17: 'RegularSizePortal', 18: 'MiniSizePortal',
              19: 'UfoPortal', 20: 'Modifier', 21: 'Breakable', 22: 'SecretCoin', 23: 'DualPortal',
              24: 'SoloPortal', 25: 'Slope', 26: 'WavePortal', 27: 'RobotPortal', 28: 'TeleportPortal',
              29: 'GreenRing', 30: 'Collectible', 31: 'UserCoin', 32: 'DropRing', 33: 'SpiderPortal',
              34: 'RedJumpPad', 35: 'RedJumpRing', 36: 'CustomRing', 37: 'DashRing', 38: 'GravityDashRing',
              39: 'CollisionObject', 40: 'Special', 41: 'SwingPortal', 42: 'GravityTogglePortal',
              43: 'SpiderOrb', 44: 'SpiderPad', 45: 'EnterEffectObject', 46: 'TeleportOrb', 47: 'AnimatedHazard'}
GAMEMODE = {5: 'ship', 6: 'cube', 16: 'ball', 19: 'ufo', 26: 'wave', 27: 'robot', 33: 'spider', 41: 'swing'}
GRAVITY = {3: 'gravity_up(inverse)', 4: 'gravity_down(normal)', 42: 'gravity_toggle'}
SIZE = {17: 'normal_size', 18: 'mini_size'}
PADS = {8: 'yellow_pad', 9: 'pink_pad', 10: 'gravity_pad(blue)', 34: 'red_pad', 44: 'spider_pad'}
ORBS = {11: 'yellow_orb', 12: 'pink_orb', 13: 'gravity_orb(blue)', 29: 'green_orb', 32: 'black_orb(drop)',
        35: 'red_orb', 36: 'custom/toggle_orb', 37: 'dash_orb', 38: 'gravity_dash_orb', 43: 'spider_orb',
        46: 'teleport_orb'}
SPEED_IDS = {200: 'speed_0.5x(slow)', 201: 'speed_1x(normal)', 202: 'speed_2x(fast)', 203: 'speed_3x(very_fast)',
             1334: 'speed_4x(super_fast)'}
EFFECT_CLASSES = {c for c in set(cls.values()) if c not in ('GameObject', 'EnhancedGameObject', 'AnimatedGameObject',
                                                             'SpecialAnimGameObject', 'SmartGameObject',
                                                             'TextGameObject', 'ParticleGameObject', 'LabelGameObject')}


def field(i, off):
    """last value written by customSetup chain then slot140 (setupCustomSprites) -> (value, source) or (None, None)"""
    val = src = None
    x = cs[i]['fields'].get(off)
    if x:
        vals = [k for k in x if k != '"<unset>"']
        if len(vals) == 1 and '"<unset>"' not in x:
            val, src = json.loads(vals[0]), 'exe:customSetup'
        elif vals:
            val, src = ('ambiguous', [json.loads(v) for v in vals]), 'exe:customSetup(path-dependent)'
    y = s140[i]['writes'].get(off)
    if y:
        vals = [k for k in y if k != 'None']
        if len(vals) == 1:
            v = vals[0]
            val, src = (int(v) if off == '0x3a0' else float(v)), 'exe:setupCustomSprites'
    return val, src


def final_type(i):
    ty, src = 0, 'exe:GameObject ctor default (0=Solid)'
    for off, v in cinit[i]:
        if off == 0x3a0:
            ty, src = v, 'exe:GameObject::createWithKey'
    if cls[i] == 'CheckpointGameObject':
        ty, src = 0x14, 'exe:CheckpointGameObject::init'
    v, s = field(i, '0x3a0')
    if v is not None and not isinstance(v, tuple):
        ty, src = int(v), s
    return ty, src


def content_size(fr):
    f = frames.get(fr)
    if not f:
        return None
    return [f['sourceSize'][0] / 4.0, f['sourceSize'][1] / 4.0]


def r2(x):
    return None if x is None else round(float(x), 3)


# ---------------------------------------------------------------- object_types.json
objects = {}
cat_count = collections.Counter()
for i in sorted(t):
    fr = t[i]
    c = cls[i]
    ty, ty_src = final_type(i)
    tn = TYPE_NAMES.get(ty)
    trig_frame = fr.startswith('edit_e') or fr == 'keyframeIcon_001.png'
    sub = None
    portal_kind = None
    if ty in GAMEMODE:
        cat, sub, portal_kind = 'portal/gamemode', GAMEMODE[ty], 'gamemode'
    elif ty in GRAVITY:
        cat, sub, portal_kind = 'gravity', GRAVITY[ty], 'gravity'
    elif ty in SIZE:
        cat, sub, portal_kind = 'size', SIZE[ty], 'size'
    elif i in SPEED_IDS:
        cat, sub, portal_kind = 'speed', SPEED_IDS[i], 'speed'
    elif ty in (14, 15):
        cat, sub, portal_kind = 'other', 'mirror_portal_' + ('on' if ty == 14 else 'off'), 'mirror'
    elif ty in (23, 24):
        cat, sub, portal_kind = 'other', 'dual_portal_' + ('on' if ty == 23 else 'off'), 'dual'
    elif ty == 28:
        cat, sub, portal_kind = 'other', 'teleport_portal', 'teleport'
    elif ty in PADS:
        cat, sub = 'orb/pad', PADS[ty]
    elif ty in ORBS:
        cat, sub = 'orb/pad', ORBS[ty]
        if i == 3643:
            sub = 'toggle_block(player_touch)'
    elif ty in (22, 31):
        cat, sub = 'collectible', 'secret_coin' if ty == 22 else 'user_coin'
    elif ty == 30:
        cat, sub = 'collectible', 'pickup_item'
    elif ty == 2 or ty == 47:
        cat, sub = 'hazard', 'animated_hazard' if ty == 47 else None
    elif ty == 25:
        haz, _ = field(i, '0x448')
        cat, sub = ('hazard', 'hazard_slope') if haz == 1 else ('solid', 'slope')
    elif ty == 21:
        cat, sub = 'solid', 'breakable'
    elif ty == 0 and not trig_frame and c == 'GameObject':
        cat = 'solid'
    elif ty in (20, 45) and (trig_frame or c in EFFECT_CLASSES):
        if c == 'StartPosObject':
            cat, sub = 'other', 'start_pos'
        elif c == 'CheckpointGameObject':
            cat, sub = 'other', 'checkpoint'
        else:
            cat, sub = 'trigger', re.sub(r'^edit_e|Btn_001\.png$|_001\.png$', '', fr)
    elif fr == 'keyframeIcon_001.png':
        cat, sub = 'trigger', 'keyframe'
    elif ty == 39:
        cat, sub = 'other', 'collision_block'
    elif ty == 40:
        cat, sub = 'other', 'special_block(D/J/S/H/F/force)'
    elif ty == 7:
        cat = 'decoration'
        if c == 'ParticleGameObject' or fr.startswith('edit_eParticle'):
            sub = 'particle_emitter'
        elif c in ('TextGameObject', 'LabelGameObject'):
            cat, sub = 'other', 'text/label'
        elif c == 'TeleportPortalObject':
            cat, sub, portal_kind = 'other', 'teleport_portal_exit', 'teleport'
    else:
        cat, sub = 'other', None
    if ty == 0 and c == 'GameObject' and trig_frame:
        cat, sub = 'other', 'editor_only?'
    cat_count[cat] += 1

    cs_size = content_size(fr)
    w, wsrc = field(i, '0x2fc')
    h, hsrc = field(i, '0x300')
    sx, _ = field(i, '0x394')
    sy, _ = field(i, '0x398')
    rad, radsrc = field(i, '0x38c')
    if isinstance(w, tuple): w = None
    if isinstance(h, tuple): h = None
    base_w = w if w is not None else (cs_size[0] if cs_size else None)
    base_h = h if h is not None else (cs_size[1] if cs_size else None)
    sxv = sx if isinstance(sx, (int, float)) else 1.0
    syv = sy if isinstance(sy, (int, float)) else 1.0
    radius = None
    if isinstance(rad, (int, float)):
        radius = float(rad)
    elif isinstance(rad, str):
        m = re.match(r'([\d.]+)\*field0x2fc', rad)
        if m and base_w is not None:
            radius = float(m.group(1)) * base_w
    hitbox = None
    collides = cat in ('solid', 'hazard', 'portal/gamemode', 'gravity', 'size', 'speed', 'orb/pad', 'collectible') \
        or portal_kind in ('mirror', 'dual', 'teleport') and sub != 'teleport_portal_exit' \
        or sub in ('collision_block', 'special_block(D/J/S/H/F/force)', 'checkpoint')
    if collides and base_w is not None:
        src = []
        src.append('m_width/m_height=' + ('exe write' if (w is not None or h is not None) else 'sprite content size (uhd sourceSize/4)'))
        if sx is not None or sy is not None:
            src.append('hitbox scale from exe customSetup')
        if radius is not None:
            hitbox = {'shape': 'circle', 'radius': r2(radius), 'w': r2(2 * radius), 'h': r2(2 * radius),
                      'basis': 'exe customSetup m_objectRadius (field 0x38c)' + (' = k*m_width' if isinstance(rad, str) else '')}
        else:
            hitbox = {'shape': 'slope' if ty == 25 else 'rect', 'w': r2(base_w * sxv), 'h': r2(base_h * syv),
                      'base_size': [r2(base_w), r2(base_h)], 'hitbox_scale': [r2(sxv), r2(syv)],
                      'basis': '; '.join(src)}
        conf = 'high' if (sx is not None or w is not None or radius is not None or cat == 'solid') else 'medium'
        if cat in ('portal/gamemode', 'gravity', 'size', 'speed') or portal_kind:
            conf = 'medium'
        if ty == 47 or c == 'AnimatedGameObject':
            conf = 'low'
            hitbox['basis'] += '; animated object - real hitbox may be set by AnimatedGameObject code not emulated'
        hitbox['confidence'] = conf
    col, colsrc = field(i, '0x450->0x4')
    if isinstance(col, tuple): col = None
    entry = {
        'frame': fr,
        'frame_exists_uhd': fr in frames,
        'class': c,
        'category': cat,
        'subtype': sub,
        'gd_type': ty,
        'gd_type_name': tn,
        'type_source': ty_src,
        'hitbox': hitbox,
        'default_base_color': int(col) if col is not None else 1004,
        'default_base_color_source': colsrc or 'exe:GameObject::init default (1004=Obj)',
        'content_size': [r2(x) for x in cs_size] if cs_size else None,
    }
    if portal_kind:
        entry['portal_kind'] = portal_kind
    inv, _ = field(i, '0x438')
    if inv == 1:
        entry['invisible_variant'] = True
    objects[str(i)] = entry

meta = {
    'generated_from': EXE_INFO + ' + Resources/*-uhd.plist (read-only, static analysis, nothing executed)',
    'method': [
        'id->frame: ObjectToolbox::init (0x1404a1ca0) map<int,gd::string> inserts, decoded with capstone',
        'class per id: emulated GameObject::createWithKey (0x140288930) switch -> vtable/RTTI class names',
        'type/hitbox/color: path-exploring emulation of <class>::customSetup (vtable slot 139: 0x14028e020 GameObject, '
        '0x1402a1f80 EnhancedGameObject, 0x14064a320 EffectGameObject family, ...) and slot 140 setupCustomSprites '
        '(0x1402acb40); last write wins (createWithKey -> customSetup -> setupCustomSprites)',
        'GameObject fields: +0x3a0 m_objectType, +0x2fc/+0x300 m_width/m_height (init = sprite content size), '
        '+0x394/+0x398 hitbox scale (init 1.0), +0x38c circular radius, +0x450->+4 base GJSpriteColor default channel, '
        '+0x448 hazardous-slope flag, +0x438 invisible-object flag (offset meanings inferred from usage, e.g. OBB builder 0x14029e470 '
        'uses m_width*scale)',
        'gd_type_name uses Geode GameObjectType enum names; numbering cross-checked against portal/orb/pad/coin ids',
    ],
    'units': 'GD units (1 block = 30). uhd sprite px / 4 = GD units (square_01_001.png is 120x120 px uhd = 30x30)',
    'hitbox_note': 'rect hitbox = m_width*scaleX x m_height*scaleY, centred on the object, before object scale/rotation. '
                   'Decoration/trigger entries have hitbox=null (no collision). Portal/orb/pad/collectible hitboxes come '
                   'from the same fields but are less battle-tested (confidence medium).',
    'category_rules': 'category from final gd_type + C++ class + frame prefix; mirror/dual/teleport portals are category '
                      '"other" with portal_kind set',
    'counts': dict(cat_count),
}
json.dump({'_meta': meta, 'objects': objects}, open('object_types.json', 'w'), indent=1)
print('object_types', len(objects), dict(cat_count))

# ---------------------------------------------------------------- deco_candidates.json
R = lambda a, b: list(range(a, b + 1))
CURATED = [
    ('outline', R(2973, 2986) + [496, 1196, 1193]),
    ('edge_line', [1753, 1754, 1757, 1756, 1004, 1005, 1003, 1191, 1197, 506, 507, 508, 513, 514, 1758, 1759,
                   1621, 1622, 1623, 1624, 1629, 1630, 1631, 1632, 2853, 2854, 2856, 2858, 2860,
                   225, 226, 500, 501, 1697, 1698, 1699, 1521, 1522, 1523, 1524, 1893, 1896,
                   237, 238, 239, 409, 410, 411]),
    ('corner', [472, 473, 474, 1205, 1206, 1207, 1223, 1224, 1225, 1261, 1263, 1265, 1194, 1192, 1002,
                2852, 2855, 2857, 2859] + R(1922, 1928) + R(1637, 1644) + R(1363, 1366)),
    ('square_fill', [211, 916, 917, 1195, 1964, 495, 279, 259, 282, 251, 1820, 1821, 1823, 1824, 1825, 1826,
                     1827, 1828, 266, 281, 273, 233, 277, 234, 278, 235, 280, 1140, 1146, 1148]),
    ('glow', [503, 504, 505, 1273, 1274, 1291, 1009, 1010, 1271, 1272, 1292, 1011, 1012, 1013, 1269, 1270, 1293,
              1228, 3825, 3828, 3829, 3843, 3958, 3959, 3960]),
    ('light', R(1367, 1376) + R(1453, 1460) + [1653, 1654, 1655, 1656, 1661, 1662, 1663, 1664] +
              [1669, 1670, 1671, 1672, 719, 721, 990, 992, 1019, 1020, 1021]),
    ('detail_small', [1764, 1765, 1766, 1767, 1768, 725, 1158, 406, 407, 408, 414, 906, 940, 941, 942,
                      1471, 1472, 1473, 1496, 1507, 1617, 1608, 1609, 1610]),
    ('geometric_shape', [236, 497, 997, 998, 999, 1000, 395, 396, 394, 150, 1830, 1831, 1832, 1833, 1834]
                        + R(3801, 3809) + [3812, 3961, 3962, 3965, 3911, 3938, 3939, 3882, 3883, 3884, 3865, 3866,
                                           229, 230, 132, 3080, 3081, 3082, 3083, 3084, 3085, 137, 138, 139, 180, 181, 182]),
    ('background_large', [48, 49, 129, 130, 131, 222, 223, 224, 936, 937, 938, 113, 114, 115, 450, 451, 452,
                          18, 19, 20, 21]),
    ('chain_or_hanging', [41, 106, 107, 110, 498, 499, 3050, 3052, 15, 16, 17, 375, 376, 377, 378]),
    ('particle_like', [3826, 3827, 3837, 3841, 3842, 3957, 3966, 3967, 3968, 3969, 3972, 3973, 3885, 3886, 3887,
                       3888, 3980, 3981, 3982, 3984, 3985, 3986, 3987, 1851, 1853, 1855, 1856, 1857, 1849]),
]
DROP = {1373, 1374, 1375, 1376, 1663, 1664, 1671, 1672,
        2856, 2860, 1893, 1896, 1523, 1524, 239, 411, 1631, 1632,
        1641, 1642, 1643, 1644, 1225, 1207, 1365, 1366,
        3084, 3085, 3882, 3884, 3865, 3866, 181, 182, 138,
        3969, 3973, 3887, 3888, 3986,
        1825, 1826, 1827, 1828, 278, 235}
cands = []
seen = set(DROP)
rejected = []
for cat, ids in CURATED:
    for i in ids:
        if i in seen or i not in t:
            continue
        seen.add(i)
        o = objects[str(i)]
        if o['category'] != 'decoration' or o['gd_type'] != 7 or o['subtype'] == 'particle_emitter':
            rejected.append((i, o['category'], o['gd_type']))
            continue
        fr = t[i]
        f = frames.get(fr)
        ps = pix.get(fr, {})
        size = [r2(f['sourceSize'][0] / 4), r2(f['sourceSize'][1] / 4)] if f else None
        vis = [r2(ps['visible_bbox_px'][0] / 4), r2(ps['visible_bbox_px'][1] / 4)] if ps.get('visible_bbox_px') else None
        sat, val = ps.get('mean_saturation'), ps.get('mean_value')
        if sat is None:
            colorable = None
        elif sat > 0.15:
            colorable = False
        elif val < 0.15:
            colorable = False
        else:
            colorable = True
        colf = fr.replace('_001.png', '_color_001.png')
        glowf = fr.replace('_001.png', '_glow_001.png')
        e = {'id': i, 'frame': fr, 'sheet': f['sheet'] if f else None, 'category': cat,
             'size_units': size, 'visible_size_units': vis,
             'colorable': colorable,
             'sprite_tone': {'mean_value': val, 'mean_saturation': sat, 'mean_alpha': ps.get('mean_alpha'),
                             'coverage': ps.get('coverage_source')},
             'default_base_color': o['default_base_color'],
             'class': o['class']}
        if colf in frames:
            e['color_child_frame'] = colf
        if glowf in frames:
            e['glow_frame'] = glowf
        if o['class'] != 'GameObject':
            e['note'] = 'EnhancedGameObject: may animate/rotate in-game' if o['class'] == 'EnhancedGameObject' else o['class']
        cands.append(e)
dmeta = {
    'generated_from': EXE_INFO + ' + Resources/*-uhd.plist/png',
    'selection': 'hand-curated from objects whose final exe-derived gd_type is 7 (Decoration), verified visually '
                 'on rendered contact sheets; categories assigned by eye + frame name',
    'size_units': 'uhd frame sourceSize / 4 (verified: square_01_001.png = 120x120 px -> 30x30 units). '
                  'visible_size_units = alpha>32 bounding box / 4',
    'colorable': 'true if sprite is grey/white (mean HSV saturation <= 0.15 and mean value >= 0.15) so the colour '
                 'channel tint shows; mean_value tells how bright the tint renders (1.0 = exact channel colour)',
    'default_base_color': 'channel id the exe assigns by default (1=colour 1, 1004=Obj, 1005=P1, 1006=P2, '
                          '1007=LBG, 1010=black, 1011=white)',
    'color_child_frame/glow_frame': 'present when a <frame>_color_001.png / <frame>_glow_001.png exists in the sheets '
                                    '(GD naming convention for detail-colour child and glow child; convention-based)',
    'count': None,
}
dmeta['count'] = len(cands)
json.dump({'_meta': dmeta, 'candidates': cands}, open('deco_candidates.json', 'w'), indent=1)
print('deco candidates', len(cands), collections.Counter(c['category'] for c in cands))
print('rejected (not pure deco):', rejected)
