#!/usr/bin/env python3
"""Checks a DSplay ARM7 ELF: code that also runs in DS mode (crt0, main RAM, ARM7 WRAM) must
never branch into .twl (DSi-only memory) except to functions it only calls in DSi mode, and
never to a linker thunk there. Usage: check_arm7.py arm7.elf"""
import re, subprocess, sys
elf = sys.argv[1]
secs = {}
for l in subprocess.run(['readelf', '-SW', elf], capture_output=True, text=True).stdout.splitlines():
    m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)', l)
    if m: secs[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
tw0, tws = secs['.twl']
twl_size = secs['.twl'][1]
bad = []
for name in ('.crt0', '.main', '.wram'):
    if name not in secs: continue
    a, n = secs[name]
    out = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', '--start-address=%#x' % a,
                          '--stop-address=%#x' % (a + n), elf], capture_output=True, text=True).stdout
    for l in out.splitlines():
        m = re.search(r'\b(bl|blx|b)\s+0x([0-9a-f]+)\s+<([^>]+)>', l)
        if m and tw0 <= int(m.group(2), 16) < tw0 + twl_size and 'Thunk' in m.group(3):
            bad.append(l.strip())
segs = []
for l in subprocess.run(['readelf', '-lW', elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if p and p[0] == 'LOAD':
        off, fsz = int(p[1], 16), int(p[4], 16)
        for o2, f2 in segs:
            if fsz and f2 and off < o2 + f2 and o2 < off + fsz:
                bad.append('segments overlap in the file at %#x / %#x' % (off, o2))
        segs.append((off, fsz))
if twl_size < 0x1000: bad.append('.twl is almost empty (%#x bytes): DSi code went to NTR memory' % twl_size)
if bad:
    print('ARM7 CHECK FAILED:'); print('\n'.join(bad)); sys.exit(1)
print('arm7 ok: .twl %#x bytes, no NTR branch into a .twl thunk' % twl_size)
