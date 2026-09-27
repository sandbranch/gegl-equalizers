#!/usr/bin/env python3
# Runs each case on generated 8 bit images three times: through the
# operation (tests/eq-apply.c), through the original plug-in's own code
# as it is, and through the plug-in's code with the changes of the
# operations made in it (tests/orig/*-fixes.patch); tests/orig-check.sh
# builds them and runs this inside the SDK.
#
# A case passes when the operation is within 1 of 255 of the plug-in
# with the changes at every pixel. How far the operation is from the
# plug-in as it is, and how many pixels the changes move, is printed as
# well: the operation differs from the original only where the changes
# act.
#
# The plug-ins' sliders are coupled in their dialogs only when asked
# (Saturation Equalizer) or always (AUMASK: neighbours at most 0.3
# apart), so the AUMASK cases use values the dialog can be set to. Its
# hidden fifth value (the factor at a difference of 0.20) is taken as a
# dialog would leave it after moving the fourth slider once from 1: the
# fourth value clamped to 1 +- 0.3.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import math
import os
import random
import subprocess
import sys

ORIG = os.environ['EQ_ORIG']
BUILD = os.environ['EQ_BUILD']
WORK = os.path.join(ORIG, 'work')
os.makedirs(WORK, exist_ok=True)
W, H = 192, 128
failed = []
totals = {'pixels': 0, 'same': 0, 'one': 0, 'orig_same': 0}


def clamp8(v):
    return max(0, min(255, int(round(v))))


def hsv(h, s, v):
    i = int(h * 6) % 6
    f = h * 6 - int(h * 6)
    p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    return [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i]


def image_colors(alpha=False):
    """hues across (with a gray band and reds on both ends), saturation in
    steps across, value down, a skin tone patch, some noise"""
    rnd = random.Random(1)
    out = bytearray()
    for y in range(H):
        for x in range(W):
            h = (x / W * 1.1 - 0.05) % 1.0
            s = [0.0, 0.03, 0.08, 0.15, 0.3, 0.5, 0.75, 1.0][(x // 3 + y // 16) % 8]
            v = 0.08 + 0.9 * y / (H - 1)
            r, g, b = hsv(h, s, v)
            if 20 <= x < 44 and 40 <= y < 64:
                r, g, b = 0.72, 0.52, 0.45
            n = (rnd.random() - 0.5) * 6
            px = [clamp8(r * 255 + n), clamp8(g * 255 + n), clamp8(b * 255 + n)]
            if alpha:
                px.append(0 if (x + y) % 13 == 0 else clamp8(255 * x / (W - 1)))
            out += bytes(px)
    return out


def image_detail(alpha=False):
    """gradients, hard shapes, thin lines, fine texture: for sharpening"""
    rnd = random.Random(2)
    out = bytearray()
    for y in range(H):
        for x in range(W):
            base = 40 + 150 * x / (W - 1)
            r = g = b = base
            if (x - 50) ** 2 + (y - 40) ** 2 < 22 ** 2:
                r, g, b = 250, 250, 245
            if (x - 50) ** 2 + (y - 40) ** 2 < 10 ** 2:
                r, g, b = 5, 5, 10
            if 100 <= x < 140 and 15 <= y < 55:
                r, g, b = 200, 40, 30
            if 145 <= x < 185 and 15 <= y < 55:
                r, g, b = 30, 90, 210
            if y > 70 and x % 9 == 0:
                r, g, b = 20, 20, 20
            if y > 70 and x > 100:
                t = (rnd.random() - 0.5) * 40
                r, g, b = 120 + t, 140 + t, 100 + t
            if 64 <= y < 67:
                r, g, b = 255, 255, 255
            n = (rnd.random() - 0.5) * 4
            px = [clamp8(r + n), clamp8(g + n), clamp8(b + n)]
            if alpha:
                px.append(clamp8(255 * y / (H - 1)))
            out += bytes(px)
    return out


def image_photo():
    """smooth color blobs, a bluish cast over everything, snow at the
    bottom"""
    rnd = random.Random(3)
    blobs = [(rnd.uniform(0, W), rnd.uniform(0, H), rnd.uniform(15, 45),
              [rnd.uniform(0.1, 0.9) for _ in range(3)]) for _ in range(9)]
    out = bytearray()
    for y in range(H):
        for x in range(W):
            c = [0.35, 0.35, 0.35]
            wsum = 1.0
            for bx, by, br, col in blobs:
                w = math.exp(-((x - bx) ** 2 + (y - by) ** 2) / (2 * br * br)) * 3
                wsum += w
                c = [c[k] + w * col[k] for k in range(3)]
            c = [v / wsum for v in c]
            if y > 100:
                c = [0.9, 0.9, 0.9]
            c = [c[0] * 0.9, c[1] * 0.97, min(1.0, c[2] * 1.08 + 0.03)]
            n = (rnd.random() - 0.5) * 5
            out += bytes(clamp8(v * 255 + n) for v in c)
    return out


IMAGES = {
    'colors': (image_colors(), 3),
    'colors_rgba': (image_colors(True), 4),
    'detail': (image_detail(), 3),
    'detail_rgba': (image_detail(True), 4),
    'photo': (image_photo(), 3),
}
for name, (data, ch) in IMAGES.items():
    with open(os.path.join(WORK, name + '.raw'), 'wb') as f:
        f.write(data)


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        print('FAIL  %s: %s' % (' '.join(cmd[:3]), p.stderr.strip()[:300]))
        return False
    return True


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def compare(a, b, ch):
    """(max difference, pixels that differ, pixels that differ by more
    than 1) over the color channels, and whether alpha is the same"""
    n = len(a) // ch
    mx = diff = big = 0
    alpha_same = True
    for i in range(n):
        d = 0
        for k in range(3):
            d = max(d, abs(a[i * ch + k] - b[i * ch + k]))
        if ch == 4 and a[i * ch + 3] != b[i * ch + 3]:
            alpha_same = False
        mx = max(mx, d)
        diff += d > 0
        big += d > 1
    return mx, diff, big, alpha_same


# Saturation Equalizer: the operation's properties and the plug-in's
# settings
SAT_MODES = {'quadratic': 'quadratic', 'safe-quadratic': 'safe', 'hsv': 'hsv',
             'hsl': 'hsl', 'yuv': 'yuv'}


def sateq_args(props):
    a = []
    for k, v in props.items():
        if k.startswith('band_'):
            a.append('boost%d=%s' % (int(k[5:]) - 1, v))
        elif k == 'color_mode':
            a.append('formula=' + SAT_MODES[v])
        elif k == 'dark':
            a.append('levels0=%s' % v)
        elif k == 'light':
            a.append('levels1=%s' % v)
        elif k.startswith('corner_'):
            a.append('corner%d=%s' % (['top_left', 'top_right', 'bottom_left',
                                       'bottom_right'].index(k[7:]), v))
        elif k == 'temperature':
            a.append('temperature=%s' % v)
        elif k == 'limit_range':
            a.append('skin=%s' % v)
        elif k == 'limit_color':
            a.append('skincolor=' + v[1:])
        elif k == 'balance':
            if v == 'layer':
                a.append('calibrate=image')
            elif v == 'color':
                a.append('calibrate=' + props['balance_color'][1:])
        elif k == 'balance_weight':
            a.append('balweight=%s' % v)
        elif k == 'balance_color':
            pass
        else:
            raise KeyError(k)
    return a


AU_OUTPUT = {'image': 'image', 'mask': 'mask', 'difference': 'difference'}


def aumask_args(props, fixed):
    a = []
    amounts = [float(props.get('amount_%d' % k, 1.0)) for k in range(1, 6)]
    for k in range(4):
        a.append('boost%d=%s' % (k, amounts[k]))
    if fixed:
        a.append('boost4=%s' % amounts[4])
    else:
        a.append('boost4=%s' % min(max(1.0, amounts[3] - 0.3), amounts[3] + 0.3))
    for k, v in props.items():
        if k.startswith('amount_'):
            continue
        name = {'radius': 'radius', 'postblur_radius': 'postradius', 'threshold': 'tresh',
                'dark': 'newdark', 'light': 'newlight', 'blur': 'blurmode',
                'source': 'colormode', 'output': 'action', 'diff_boost': 'mask_boost',
                'central_brightness': 'mask_pos'}[k]
        a.append('%s=%s' % (name, v))
    return a


def case(kind, label, image, props, port_props=None):
    data, ch = IMAGES[image]
    src = os.path.join(WORK, image + '.raw')
    outs = {}
    for v in ('orig', 'fixed'):
        prog = os.path.join(ORIG, v, 'sateq' if kind == 'sateq' else 'aumask')
        args = sateq_args(props) if kind == 'sateq' else aumask_args(props, v == 'fixed')
        path = os.path.join(WORK, '%s-%s-%s.raw' % (kind, label, v))
        if not run([prog, src, str(W), str(H), str(ch), path] + args):
            failed.append(label)
            return
        outs[v] = read(path)
    op = 'equalizer:saturation' if kind == 'sateq' else 'equalizer:sharpness'
    pp = dict(props if port_props is None else port_props)
    path = os.path.join(WORK, '%s-%s-port.raw' % (kind, label))
    if not run([os.path.join(BUILD, 'eq-apply'), BUILD, op, str(W), str(H), str(ch), src, path]
               + ['%s=%s' % (k, v) for k, v in pp.items()]):
        failed.append(label)
        return
    port = read(path)
    mf, df, bf, af = compare(port, outs['fixed'], ch)
    mo, do, bo, ao = compare(port, outs['orig'], ch)
    mx, dx, bx, ax = compare(outs['fixed'], outs['orig'], ch)
    n = len(port) // ch
    ok = bf == 0 and af and (ch == 3 or data[3::4] == port[3::4])
    totals['pixels'] += n
    totals['same'] += n - df
    totals['one'] += df - bf
    totals['orig_same'] += n - do
    print('%s  %s_%s: vs the original with the fixes max %d (%d of %d pixels differ, '
          '%d by more than 1); vs the original max %d (%d differ)%s'
          % ('PASS' if ok else 'FAIL', kind, label, mf, df, n, bf, mo, do,
             '' if ch == 3 else ', alpha %s' % ('kept' if ok else 'CHANGED')))
    if dx:
        print('NOTE  %s_%s: the fixes change %d pixels of the original (max %d)'
              % (kind, label, dx, mx))
    if not ok:
        failed.append(label)


UP = {'band_1': 2.0, 'band_2': 1.8, 'band_3': 1.6, 'band_4': 1.4, 'band_5': 1.2, 'band_6': 1.0}
DOWN = {'band_1': 0.2, 'band_2': 0.4, 'band_3': 0.6, 'band_4': 0.8, 'band_5': 0.9, 'band_6': 1.0}

case('sateq', 'defaults', 'colors', {})
for k in range(1, 7):
    case('sateq', 'band_%d' % k, 'colors', {'band_%d' % k: 2.5})
for m in SAT_MODES:
    case('sateq', 'up_' + m, 'colors', dict(UP, color_mode=m))
    case('sateq', 'down_' + m, 'colors', dict(DOWN, color_mode=m))
case('sateq', 'levels_in', 'colors', {'dark': 0.1, 'light': 0.9})
case('sateq', 'levels_out', 'colors', {'dark': -0.3, 'light': 1.3, 'band_3': 1.5})
case('sateq', 'levels_hsv', 'colors', {'dark': 0.2, 'light': 1.2, 'color_mode': 'hsv'})
case('sateq', 'corners', 'colors', {'corner_top_left': 0.1, 'corner_top_right': -0.1,
                                    'corner_bottom_left': 0.05, 'corner_bottom_right': -0.2})
for m, t in (('quadratic', 0.5), ('hsv', -0.8), ('yuv', 1.0), ('hsl', 0.3),
             ('safe-quadratic', -0.5)):
    case('sateq', 'temperature_' + m, 'colors', {'temperature': t, 'color_mode': m,
                                                   'band_2': 1.5})
# the plug-in's default color is 43010, 28305, 24990 of 65535, the
# operation's #a76e61, the nearest 8 bit color: the default case shows
# the difference (the plug-in with the fixes divides by 65535, the
# original by 65536), the others give the color
case('sateq', 'limit_default_color', 'colors', dict(UP, color_mode='yuv', limit_range=0.04))
case('sateq', 'limit_yuv', 'colors', dict(UP, color_mode='yuv', limit_range=0.04,
                                          limit_color='#a76e61'))
for m in ('quadratic', 'hsv', 'hsl'):
    case('sateq', 'limit_' + m, 'colors', dict(UP, color_mode=m, limit_range=0.06,
                                               limit_color='#a76e61'))
case('sateq', 'limit_color', 'colors', dict(UP, color_mode='yuv', limit_range=0.05,
                                            limit_color='#3060c0'))
case('sateq', 'limit_inverted', 'colors', dict(UP, color_mode='yuv', limit_range=-0.04,
                                               limit_color='#a76e61'))
case('sateq', 'limit_inverted_hsv', 'colors', dict(UP, color_mode='hsv', limit_range=-0.05,
                                                   limit_color='#c03040'))
case('sateq', 'balance_layer', 'photo', {'balance': 'layer', 'balance_weight': 1.0})
case('sateq', 'balance_half', 'photo', {'balance': 'layer', 'balance_weight': 0.5,
                                        'band_2': 1.4})
case('sateq', 'balance_color', 'photo', {'balance': 'color', 'balance_color': '#d8e0f0',
                                         'balance_weight': 1.0})
case('sateq', 'safe_strong', 'colors', {'band_1': 4.0, 'band_2': 3.75, 'band_3': 3.5,
                                        'band_4': 3.0, 'color_mode': 'safe-quadratic'})
case('sateq', 'everything', 'photo', dict(UP, dark=0.05, light=0.95, temperature=-0.3,
                                          corner_top_left=0.05, corner_bottom_right=-0.05,
                                          balance='layer', balance_weight=0.7,
                                          limit_range=0.07, limit_color='#a76e61'))
case('sateq', 'rgba', 'colors_rgba', dict(UP, temperature=0.4))
case('sateq', 'photo_down', 'photo', dict(DOWN, color_mode='hsl'))

AMOUNTS = {'amount_1': 1.2, 'amount_2': 1.5, 'amount_3': 1.8, 'amount_4': 2.0, 'amount_5': 1.7}
SOFT = {'amount_1': 0.8, 'amount_2': 0.5, 'amount_3': 0.3, 'amount_4': 0.2, 'amount_5': 0.5}

case('aumask', 'defaults', 'detail', {})
case('aumask', 'amounts', 'detail', AMOUNTS)
for r in (1, 5, 60, 500):
    case('aumask', 'radius_%d' % r, 'detail', dict(AMOUNTS, radius=r))
case('aumask', 'soften', 'detail', SOFT)
for t in (0.1, 0.3):
    case('aumask', 'selective_%g' % t, 'detail', dict(AMOUNTS, blur='selective', threshold=t))
for r in (0, 3, 10):
    case('aumask', 'postblur_%d' % r, 'detail', dict(AMOUNTS, blur='postblur',
                                                     postblur_radius=r, threshold=0.15))
for s in ('value', 'lightness', 'red', 'green', 'blue'):
    case('aumask', 'source_' + s, 'detail', dict(AMOUNTS, source=s))
case('aumask', 'source_red_neutral', 'colors', {'source': 'red'})
case('aumask', 'levels', 'detail', dict(AMOUNTS, dark=0.1, light=0.9))
case('aumask', 'levels_hsl', 'detail', dict(AMOUNTS, dark=-0.2, light=1.2, source='lightness'))
for s in ('brightness', 'value', 'lightness', 'green'):
    case('aumask', 'mask_' + s, 'detail', {'output': 'mask', 'source': s, 'dark': 0.1})
for s in ('brightness', 'lightness'):
    case('aumask', 'difference_' + s, 'detail', {'output': 'difference', 'source': s})
case('aumask', 'difference_boost', 'detail', {'output': 'difference', 'diff_boost': -3.0,
                                              'central_brightness': 0.3, 'radius': 8})
case('aumask', 'rgba', 'detail_rgba', AMOUNTS)
case('aumask', 'colors', 'colors', dict(AMOUNTS, radius=10, blur='selective'))
case('aumask', 'photo', 'photo', dict(AMOUNTS, radius=30, source='value'))

p = totals['pixels']
print('NOTE  all cases: %d pixels; the operation equals the original with the fixes at '
      '%.3f %%, differs by 1 at %.3f %%; equals the original as it is at %.3f %%'
      % (p, 100.0 * totals['same'] / p, 100.0 * totals['one'] / p,
         100.0 * totals['orig_same'] / p))
print('%d failed' % len(failed))
sys.exit(1 if failed else 0)
