#!/usr/bin/env python3
"""Scan a firmware lib tree for unresolvable App-level C++ symbols.

Usage: abi_scan.py <libdir> [--extra-syms <file>] [--verbose]

Filters out known false positives:
  * symbols versioned @GLIBC*/@CXXABI*/@GLIBCXX* (versioned names never appear as
    plain undefined names in .dynsym, but keep the filter for safety)
  * libstdc++/libc internals (operator new/delete, std::*, __cxa_*, _Unwind_*)
  * _Jv_RegisterClasses / __gmon_start__ / _ITM_*  (linker artifacts)
Resolution also considers definitions found in ANY .so of the tree plus optional
extra symbol files (e.g. libstdc++.so.6 symbol dump) supplied via --extra-syms.
"""
import os, re, subprocess, sys

NM = os.environ.get('NM', 'arm-linux-gnueabihf-nm')
FILTER_PREFIX = ('_ZdaPv', '_ZdlPv', '_Znwj', '_Znaj', '_Znwm', '_Znam', '_ZSt',
                 '__cxa_', '_Unwind_', '_Jv_', '__gmon_start__', '_ITM_', '_ZTVSt',
                 '_ZTISt', '_ZTSSt', '_ZNSt', '_ZNKSt', '_ZSt')
LIBSTD_RE = re.compile(r'^_Z(N?K?St|nw|na|dl|da)')

def strip_ver(s):
    return s.split('@', 1)[0]

def syms(path, flag):
    try:
        out = subprocess.run([NM, '-D', flag, path], capture_output=True, text=True, timeout=60).stdout
    except Exception:
        return set()
    res = set()
    for line in out.splitlines():
        p = line.split()
        if p:
            res.add(strip_ver(p[-1]))
    return res

def main():
    if len(sys.argv) < 2:
        print(__doc__); return 1
    root = sys.argv[1]
    extra = []
    if '--extra-syms' in sys.argv:
        extra = sys.argv[sys.argv.index('--extra-syms') + 1].split(',')
    files = sorted(f for f in os.listdir(root) if f.endswith('.so'))
    defined = set()
    for f in files:
        defined |= syms(os.path.join(root, f), '--defined-only')
    for x in extra:
        defined |= syms(x, '--defined-only')
    print("# tree: %s  (%d .so, %d defined symbols)" % (root, len(files), len(defined)))
    print("# extra symbol sources: %s" % (extra or 'none'))
    total = 0
    for f in files:
        miss = []
        for s in syms(os.path.join(root, f), '--undefined-only'):
            if not s.startswith('_Z'):
                continue
            if s in defined or s.startswith(FILTER_PREFIX):
                continue
            miss.append(s)
        if miss:
            total += 1
            print("\n%s  (%d)" % (f, len(miss)))
            for s in sorted(miss)[:24]:
                print("   %s" % s)
    print("\n# libraries with unresolved App C++ symbols: %d" % total)
    return 0

if __name__ == '__main__':
    sys.exit(main())
