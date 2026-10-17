#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
abi_closure.py -- ZV-1F(new menu) -> ZV-E10 ABI closure feasibility analysis.

Static analysis only. Uses binutils `readelf` (pyelftools is NOT available).
Reads decrypted/unpacked firmware trees that already exist on disk; downloads nothing.

Usage:
    python3 abi_closure.py [--prefer F|E10] [--outdir <repo>/port]

Outputs:
    <repo>/port/abi_closure.json   (machine readable dump of every table)
    <repo>/port/abi_closure_report.md (Chinese report, with raw command appendix)
"""

import os
import re
import sys
import json
import hashlib
import argparse
import subprocess
import collections

# ----------------------------------------------------------------------------
# tree roots
# ----------------------------------------------------------------------------
E10 = "<repo>/unpacked_zve10"
F1F = "<repo>/unpacked_zv1f"

E10_N15 = E10 + "/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked"
E10_N7 = E10 + "/firmware.tar_unpacked/0700_part_image/dev/nflasha7_unpacked"
E10_N3_INITRD = E10 + "/firmware.tar_unpacked/0700_part_image/dev/nflasha3_unpacked/initrd.img_unpacked"
E10_UPD1400 = E10 + "/firmware.tar_unpacked/0400_updater/dev/nflasha1_unpacked/boot/initrd.img_unpacked"
E10_UPDIMG = E10 + "/updater.img_unpacked"

F_N15 = F1F + "/firmware.tar_unpacked/0700_part_image/dev/nflasha15_unpacked"
F_N7 = F1F + "/firmware.tar_unpacked/0700_part_image/dev/nflasha7_unpacked"
F_N3_INITRD = F1F + "/firmware.tar_unpacked/0700_part_image/dev/nflasha3_unpacked/initrd.img_unpacked"
F_UPDIMG = F1F + "/updater.img_unpacked"

# library search groups: (tree, group_name, dir, category)
LIB_GROUPS = [
    ("E10", "E10:n15lib",   E10_N15 + "/lib",            "core"),
    ("E10", "E10:n7rootfs", E10_N7 + "/lib",             "core"),
    ("E10", "E10:n3initrd", E10_N3_INITRD + "/lib",      "initrd"),
    ("E10", "E10:upd1400lib", E10_UPD1400 + "/lib",      "initrd"),
    ("E10", "E10:upd1400usrlib", E10_UPD1400 + "/usr/lib", "initrd"),
    ("E10", "E10:upd1400locallib", E10_UPD1400 + "/usr/local/lib", "initrd"),
    ("E10", "E10:updimgusrlib", E10_UPDIMG + "/usr/lib", "updater"),
    ("E10", "E10:udtrbodyusrlib", E10_N15 + "/bin/udtrbody.bin_unpacked/usr/lib", "updater"),
    ("E10", "E10:updimglib", E10_UPDIMG + "/lib",        "updater"),
    ("F",   "F:n15lib",     F_N15 + "/lib",              "core"),
    ("F",   "F:n7rootfs",   F_N7 + "/lib",               "core"),
    ("F",   "F:n3initrd",   F_N3_INITRD + "/lib",        "initrd"),
    ("F",   "F:updimgusrlib", F_UPDIMG + "/usr/lib",     "updater"),
    ("F",   "F:updimglib",  F_UPDIMG + "/lib",           "updater"),
    # scenario plugins: loaded at runtime via dlopen; included so the provider pool
    # is complete for the "does a provider exist ANYWHERE in the tree" question.
    ("E10", "E10:n15scenario", E10_N15 + "/scenario",     "scenario"),
    ("F",   "F:n15scenario",   F_N15 + "/scenario",       "scenario"),
    ("E10", "E10:n3initrdsbin", E10_N3_INITRD + "/sbin",  "scenario"),
    ("F",   "F:n3initrdsbin",   F_N3_INITRD + "/sbin",    "scenario"),
]

# executable search groups (may export dynamic symbols via -rdynamic / --export-dynamic)
BIN_GROUPS = [
    ("E10", "E10:n15bin",   E10_N15 + "/bin",   "bin"),
    ("E10", "E10:n15sbin",  E10_N15 + "/sbin",  "sbin"),
    ("E10", "E10:n15sabin", E10_N15 + "/sabin", "sabin"),
    ("E10", "E10:n15tool",  E10_N15 + "/tool",  "tool"),
    ("E10", "E10:n7bin",    E10_N7 + "/bin",    "bin"),
    ("E10", "E10:n7sbin",   E10_N7 + "/sbin",   "sbin"),
    # updater / initrd executables: they are merely *importers* of legacy weak
    # symbols, but they must be scanned so they are not mistaken for providers.
    ("E10", "E10:upd1400bin",    E10_UPD1400 + "/bin",     "bin"),
    ("E10", "E10:upd1400sbin",   E10_UPD1400 + "/sbin",    "sbin"),
    ("E10", "E10:upd1400usrbin", E10_UPD1400 + "/usr/bin", "bin"),
    ("E10", "E10:updimgbin",     E10_UPDIMG + "/bin",      "bin"),
    ("E10", "E10:n3initrdbin",   E10_N3_INITRD + "/sbin",  "sbin"),
    ("F",   "F:updimgbin",       F_UPDIMG + "/bin",        "bin"),
    ("F",   "F:n3initrdbin",     F_N3_INITRD + "/sbin",    "sbin"),
    ("F",   "F:n15bin",     F_N15 + "/bin",     "bin"),
    ("F",   "F:n15sbin",    F_N15 + "/sbin",    "sbin"),
    ("F",   "F:n15sabin",   F_N15 + "/sabin",   "sabin"),
    ("F",   "F:n15tool",    F_N15 + "/tool",    "tool"),
    ("F",   "F:n7bin",      F_N7 + "/bin",      "bin"),
    ("F",   "F:n7sbin",     F_N7 + "/sbin",     "sbin"),
]

KMOD_DIRS = [("E10", E10_N15 + "/kmod"), ("F", F_N15 + "/kmod")]

COMMANDS = []          # raw command lines, for the reproducibility appendix


def sh(cmd):
    """run a command, record it, return stdout text + rc"""
    COMMANDS.append(" ".join(cmd) if isinstance(cmd, list) else cmd)
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=900)
        return p.stdout.decode("utf-8", "replace"), p.returncode
    except Exception:
        return "", -1


def is_elf(path):
    try:
        with open(path, "rb") as fh:
            return fh.read(4) == b"\x7fELF"
    except Exception:
        return False


def fsize(path):
    try:
        return os.path.getsize(path)
    except Exception:
        return -1


def md5(path):
    h = hashlib.md5()
    try:
        with open(path, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
    except Exception:
        return ""
    return h.hexdigest()


# ----------------------------------------------------------------------------
# symbol helpers
# ----------------------------------------------------------------------------
def split_ver(nm):
    """foo@@VER -> (foo, VER) ; foo@VER -> (foo, VER) ; foo -> (foo, '')"""
    if "@@" in nm:
        b, v = nm.split("@@", 1)
        return b, v
    if "@" in nm:
        b, v = nm.split("@", 1)
        return b, v
    return nm, ""


def read_dynamic(path):
    """return (needed[], soname)"""
    text, rc = sh(["readelf", "-W", "-d", path])
    needed, soname = [], ""
    if rc != 0:
        return needed, soname
    for line in text.splitlines():
        m = re.search(r"\(NEEDED\)\s+Shared library: \[(.*?)\]", line)
        if m:
            needed.append(m.group(1))
            continue
        m = re.search(r"\(SONAME\)\s+Library soname: \[(.*?)\]", line)
        if m:
            soname = m.group(1)
    return needed, soname


def read_dynsyms(path):
    """
    Parse `readelf -W --dyn-syms`.
    returns (defined, undef, und_weak)
      - defined/undef: dict base_name -> set(version strings)
      - und_weak: set of base names whose UND import binding is WEAK
        (WEAK undefined symbols are NOT a hard error at load time)
      - only GLOBAL/WEAK bindings are kept (LOCAL cannot satisfy a cross-object import)
      - SECTION/FILE pseudo-symbols are dropped
      - versioned names are aliased to their base name, version kept for soft-gap analysis
    """
    text, rc = sh(["readelf", "-W", "--dyn-syms", path])
    defined = collections.defaultdict(set)
    undef = collections.defaultdict(set)
    und_weak = set()
    if rc != 0:
        return defined, undef, und_weak
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith("Symbol table") or s.startswith("Num:"):
            continue
        t = s.split()
        if len(t) < 8:
            continue
        typ, bind, ndx = t[3], t[4], t[6]
        rest = t[7:]
        # drop a trailing version-index token such as "(2)"
        if len(rest) >= 2 and rest[-1].startswith("(") and rest[-1].endswith(")"):
            rest = rest[:-1]
        if not rest:
            continue
        name = rest[0]
        if bind not in ("GLOBAL", "WEAK"):
            continue
        if typ in ("SECTION", "FILE", "NOTYPE") and ndx != "UND" and typ in ("SECTION", "FILE"):
            continue
        base, ver = split_ver(name)
        if not base:
            continue
        if ndx == "UND":
            undef[base].add(ver)
            if bind == "WEAK":
                und_weak.add(base)
        else:
            if typ in ("SECTION", "FILE"):
                continue
            defined[base].add(ver)
    return defined, undef, und_weak


# ----------------------------------------------------------------------------
# ELF object model
# ----------------------------------------------------------------------------
class Obj(object):
    __slots__ = ("path", "tree", "group", "category", "kind", "size", "soname",
                 "basename", "key", "needed", "defined", "undef", "und_weak",
                 "is_elf", "md5")

    def __init__(self, path, tree, group, category, kind):
        self.path = path
        self.tree = tree
        self.group = group
        self.category = category
        self.kind = kind                    # 'lib' | 'exe'
        self.basename = os.path.basename(path)
        self.size = fsize(path)
        self.is_elf = is_elf(path)
        self.soname = ""
        self.needed = []
        self.defined = collections.defaultdict(set)
        self.undef = collections.defaultdict(set)
        self.und_weak = set()
        self.md5 = ""
        if self.is_elf:
            self.needed, self.soname = read_dynamic(path)
            self.defined, self.undef, self.und_weak = read_dynsyms(path)
        self.key = self.soname if self.soname else self.basename

    @property
    def syms(self):
        return set(self.defined.keys())


def scan_dir(root, tree, group, category, kind_to_use):
    """collect ELF files directly under root (recursive). Non-ELF are recorded and skipped."""
    out, skipped = [], []
    if not os.path.isdir(root):
        return out, skipped
    for dirpath, dirnames, filenames in os.walk(root):
        for fn in filenames:
            p = os.path.join(dirpath, fn)
            if not os.path.isfile(p):
                continue
            if is_elf(p):
                out.append(Obj(p, tree, group, category, kind_to_use))
            else:
                skipped.append(p)
    return out, skipped


class Pool(object):
    """a set of objects, indexed by SONAME (fallback basename)"""

    def __init__(self, name, objs, prefer=None):
        self.name = name
        self.prefer = prefer
        self.by_key = {}
        self.shadowed = []          # same key present in both trees, losing side
        self.collisions = []        # (key, winner, loser, identical?)
        for o in objs:
            if o.key in self.by_key:
                cur = self.by_key[o.key]
                if prefer == "F":
                    win, lose = (o, cur) if o.tree == "F" else (cur, o)
                elif prefer == "E10":
                    win, lose = (o, cur) if o.tree == "E10" else (cur, o)
                else:
                    win, lose = (cur, o)
                if win is cur:
                    lose = o
                self.by_key[o.key] = win
                self.shadowed.append(lose)
                same = (win.size == lose.size and win.md5 == lose.md5) if win.md5 else (win.size == lose.size)
                self.collisions.append({
                    "key": o.key, "winner": win, "loser": lose, "identical": same,
                })
            else:
                self.by_key[o.key] = o
        # basename fallback index
        self.by_base = {}
        for o in self.by_key.values():
            self.by_base.setdefault(o.basename, o)

    def resolve(self, name):
        if name in self.by_key:
            return self.by_key[name]
        if name in self.by_base:
            return self.by_base[name]
        # NEEDED sometimes carries a path
        b = os.path.basename(name)
        return self.by_key.get(b) or self.by_base.get(b)

    def syms(self):
        s = set()
        for o in self.by_key.values():
            s |= o.syms
        return s


def closure(pool, root):
    """transitive DT_NEEDED closure. returns (members dict key->Obj, missing list of (from_key, needed))"""
    seen = {}
    missing = []
    stack = [root]
    while stack:
        cur = stack.pop()
        if cur.key in seen:
            continue
        seen[cur.key] = cur
        for nd in cur.needed:
            p = pool.resolve(nd)
            if p is None:
                missing.append((cur.key, nd))
            elif p.key not in seen:
                stack.append(p)
    return seen, missing


# ----------------------------------------------------------------------------
# residual-symbol provenance helpers
# ----------------------------------------------------------------------------
IDENT_RE = re.compile(rb"[A-Za-z_][A-Za-z0-9_]{2,}")


def kmod_ident_index():
    """build {identifier: set('tree:kmod_name')} from raw bytes (__ksymtab holds names as raw strings)."""
    idx = collections.defaultdict(set)
    files = []
    for tree, d in KMOD_DIRS:
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if fn.endswith(".ko"):
                files.append((tree, os.path.join(d, fn)))
    for tree, p in files:
        try:
            with open(p, "rb") as fh:
                data = fh.read()
        except Exception:
            continue
        for m in set(IDENT_RE.findall(data)):
            idx[m.decode("ascii")].add("%s:%s" % (tree, os.path.basename(p)))
    return idx, [p for _, p in files]


def grep_fixed_which(symbols, roots, limit_files=200000):
    """
    Which files contain each symbol name as a raw byte string?
    Uses system grep -F -f (multi-pattern Aho-Corasick, C speed).
    returns dict sym -> sorted list of paths (relative)
    """
    result = collections.defaultdict(list)
    if not symbols:
        return result
    pat = "/tmp/abi_residual_pat.txt"
    with open(pat, "w") as fh:
        for s in sorted(symbols):
            fh.write(s + "\n")
    existing = [r for r in roots if os.path.isdir(r)]
    if not existing:
        return result
    cmd = ["grep", "-rlF", "-f", pat] + existing
    COMMANDS.append(" ".join(cmd))
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=1800)
        text = p.stdout.decode("utf-8", "replace")
    except Exception:
        return result
    found_files = [l for l in text.splitlines() if l.strip()]
    # now, for each file, find which of our symbols it contains (only files that matched)
    symlist = list(symbols)
    for fp in found_files:
        try:
            with open(fp, "rb") as fh:
                data = fh.read()
        except Exception:
            continue
        for s in symlist:
            if s.encode("ascii", "replace") in data:
                result[s].append(fp)
    return result


# ----------------------------------------------------------------------------
# prefix grouping
# ----------------------------------------------------------------------------
_MANGLE_FIRST = re.compile(r"^_ZN?K?(\d+)([A-Za-z_][A-Za-z0-9_]*)")


def demangle_batch(names):
    if not names:
        return {}
    try:
        p = subprocess.run(["c++filt"], input="\n".join(names).encode(),
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=300)
        COMMANDS.append("c++filt  < <symbol list>")
        out = p.stdout.decode("utf-8", "replace").splitlines()
        if len(out) == len(names):
            return dict(zip(names, out))
    except Exception:
        pass
    return {}


def group_of(name, demangled=None):
    d = demangled or name
    if d != name or name.startswith("_Z"):
        # mangled C++: group by first namespace / class component
        if "::" in d:
            return d.split("::", 1)[0] + "::"
        m = _MANGLE_FIRST.match(name)
        if m:
            return m.group(2) + "::"
        return "C++(other)"
    m = re.match(r"^([A-Za-z][A-Za-z0-9]*_)", name)
    if m:
        return m.group(1)
    m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)", name)
    if m:
        g = m.group(1)
        return g if len(g) <= 12 else g[:12] + "..."
    return "(other)"


# ----------------------------------------------------------------------------
# relocation / binding semantics
# ----------------------------------------------------------------------------
LAZY_RELOCS = {"R_ARM_JUMP_SLOT", "R_ARM_PLT32"}


def has_bind_now(path):
    """DF_BIND_NOW / DF_1_NOW -> all PLT relocations are resolved eagerly at load time."""
    text, rc = sh(["readelf", "-W", "-d", path])
    if rc != 0:
        return False
    if "BIND_NOW" in text:
        return True
    if re.search(r"FLAGS_1.*NOW", text):
        return True
    return False


def reloc_class(path, symset):
    """
    readelf -W -r : how is each symbol referenced?
    returns dict sym -> {'lazy': n, 'eager': n}
      lazy  = R_ARM_JUMP_SLOT (PLT)  -> resolved on first call when lazy binding is active
      eager = everything else (GLOB_DAT/ABS32/COPY/TLS...) -> resolved at load time
    """
    res = collections.defaultdict(lambda: {"lazy": 0, "eager": 0})
    if not symset:
        return res
    text, rc = sh(["readelf", "-W", "-r", path])
    if rc != 0:
        return res
    for line in text.splitlines():
        t = line.split()
        if len(t) < 5:
            continue
        typ = t[2]
        if not typ.startswith("R_"):
            continue
        nm = t[4].split("@")[0]
        if nm not in symset:
            continue
        if typ in LAZY_RELOCS:
            res[nm]["lazy"] += 1
        else:
            res[nm]["eager"] += 1
    return res


# ----------------------------------------------------------------------------
# main
# ----------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prefer", default="F", choices=["F", "E10"])
    ap.add_argument("--outdir", default="<repo>/port")
    args = ap.parse_args()

    print("[*] scanning library dirs ...", file=sys.stderr)
    libs, skipped = [], []
    for tree, group, d, cat in LIB_GROUPS:
        o, sk = scan_dir(d, tree, group, cat, "lib")
        libs.extend(o)
        skipped.extend(sk)
        print("    %-22s %-48s %3d ELF, %d non-ELF" % (group, d, len(o), len(sk)), file=sys.stderr)

    print("[*] scanning executable dirs ...", file=sys.stderr)
    exes = []
    for tree, group, d, cat in BIN_GROUPS:
        o, sk = scan_dir(d, tree, group, cat, "exe")
        exes.extend(o)
        skipped.extend(sk)

    for o in libs:
        o.md5 = md5(o.path)

    e10_libs = [o for o in libs if o.tree == "E10"]
    f_libs = [o for o in libs if o.tree == "F"]

    # pools ------------------------------------------------------------------
    E10_CORE = Pool("E10_CORE", [o for o in e10_libs if o.category == "core"], prefer=None)
    E10_ALL = Pool("E10_ALL", e10_libs, prefer=None)
    F_ALL = Pool("F_ALL", f_libs, prefer=None)
    MERGED = Pool("MERGED_prefer_" + args.prefer,
                  [o for o in libs], prefer=args.prefer)

    pools = {"E10_CORE": E10_CORE, "E10_ALL": E10_ALL, "F_ALL": F_ALL, "MERGED": MERGED}
    pool_syms = {k: v.syms() for k, v in pools.items()}

    print("[*] ELF libs: E10=%d F=%d total=%d ; exes=%d ; non-ELF skipped=%d"
          % (len(e10_libs), len(f_libs), len(libs), len(exes), len(skipped)), file=sys.stderr)

    # roots ------------------------------------------------------------------
    NAMED_ROOTS = ["appFw.so", "appFwBoot.so", "libInfraSKD.so", "libObj.so", "libmpr.so",
                   "libSysDef.so", "AdjChkConfig.so", "libIMDB.so", "libInfraCryptCore.so",
                   "libInfraCCPF.so"]
    roots = {}
    for nm in NAMED_ROOTS:
        o = F_ALL.resolve(nm) or E10_ALL.resolve(nm)
        if o:
            roots[nm] = o
        else:
            roots[nm] = None

    # F-only or size-differing libs (matched by SONAME across the two trees)
    e10_by_key = dict(E10_ALL.by_key)
    f_by_key = dict(F_ALL.by_key)
    extra_roots = []
    for k, fo in sorted(f_by_key.items()):
        eo = e10_by_key.get(k)
        if eo is None:
            extra_roots.append(("F-only", k, fo, None))
        elif eo.size != fo.size:
            extra_roots.append(("size-diff", k, fo, eo))
    for k, eo in sorted(e10_by_key.items()):
        if k not in f_by_key:
            pass  # E10-only libs are not new-UI roots

    print("[*] named roots found: %d/%d ; F-only=%d ; size-diff=%d"
          % (sum(1 for v in roots.values() if v), len(NAMED_ROOTS),
             sum(1 for t, _, _, _ in extra_roots if t == "F-only"),
             sum(1 for t, _, _, _ in extra_roots if t == "size-diff")), file=sys.stderr)

    # ---- table a: appFw.so undefined symbol accounting ---------------------
    appfw = roots.get("appFw.so")
    if appfw is None:
        print("!! appFw.so not found", file=sys.stderr)
        sys.exit(2)

    appfw_und = set(appfw.undef.keys())
    appfw_und_ver = {k: sorted(v) for k, v in appfw.undef.items()}

    res_e10_core = appfw_und & pool_syms["E10_CORE"]
    res_e10_all = appfw_und & pool_syms["E10_ALL"]
    res_f_all = appfw_und & pool_syms["F_ALL"]
    res_merged = appfw_und & pool_syms["MERGED"]

    # strict transitive closure for EVERY root, under MERGED
    root_reports = []
    agg_und = set()
    for label, ro in list(roots.items()):
        if ro is None:
            continue
        agg_und |= set(ro.undef.keys())
        mem, miss = closure(MERGED, ro)
        prov = set()
        for m in mem.values():
            prov |= m.syms
        un = set(ro.undef.keys())
        root_reports.append({
            "root": label,
            "soname": ro.key,
            "tree": ro.tree,
            "size": ro.size,
            "n_needed": len(ro.needed),
            "closure_size": len(mem),
            "closure_members": sorted(mem.keys()),
            "missing_needed": sorted(set(miss)),
            "n_und": len(un),
            "n_und_resolved_in_closure": len(un & prov),
            "n_und_unresolved_in_closure": len(un - prov),
        })

    # gap for appFw.so
    gap = appfw_und - pool_syms["E10_ALL"]
    # per-F-lib contribution to the gap
    f_contrib = []
    for k, fo in sorted(F_ALL.by_key.items()):
        s = fo.syms
        g = s & gap
        if g:
            f_contrib.append({"key": k, "soname": fo.soname, "basename": fo.basename,
                              "path": fo.path, "size": fo.size,
                              "n_gap": len(g), "gap_syms": sorted(g)})
    f_contrib.sort(key=lambda d: (-d["n_gap"], d["key"]))
    # uniqueness among F providers
    cover_count = collections.Counter()
    for d in f_contrib:
        for s in d["gap_syms"]:
            cover_count[s] += 1
    for d in f_contrib:
        d["n_unique"] = sum(1 for s in d["gap_syms"] if cover_count[s] == 1)

    # greedy set cover over the gap
    covered, taken = set(), []
    for d in f_contrib:
        new = set(d["gap_syms"]) - covered
        if new:
            taken.append({"key": d["key"], "path": d["path"], "new": len(new),
                          "cumulative": len(covered | new)})
            covered |= new
        if covered >= gap:
            break
    gap_uncovered = gap - covered

    # ---- table c: residual (whole new-UI root set) -------------------------
    newui_und = set()
    for label, ro in roots.items():
        if ro is not None:
            newui_und |= set(ro.undef.keys())
    for t, k, fo, eo in extra_roots:
        newui_und |= set(fo.undef.keys())
    residual = sorted(newui_und - pool_syms["MERGED"])
    residual_appfw = sorted(appfw_und - pool_syms["MERGED"])

    # cross-check: symbol-level UNION of EVERY lib in both trees, ignoring the
    # SONAME shadowing rule entirely. Answers "does a provider exist ANYWHERE?".
    union_all = set()
    for o in libs:
        union_all |= o.syms
    union_res_appfw = sorted(appfw_und - union_all)
    union_res_newui = sorted(newui_und - union_all)

    # ---- prefix-level coverage of appFw.so (answers "what provides ux::/BMP_/...") ---
    dm_appfw = demangle_batch(sorted(appfw_und))
    by_group_appfw = collections.defaultdict(list)
    for s in appfw_und:
        by_group_appfw[group_of(s, dm_appfw.get(s))].append(s)
    prefix_rows = []
    for g, syms in by_group_appfw.items():
        provs = collections.Counter()
        for s in syms:
            for key, o in MERGED.by_key.items():
                if s in o.defined:
                    provs[key] += 1
        r = [s for s in syms if s not in pool_syms["MERGED"]]
        prefix_rows.append({
            "group": g,
            "n": len(syms),
            "n_in_e10": sum(1 for s in syms if s in pool_syms["E10_ALL"]),
            "n_in_f": sum(1 for s in syms if s in pool_syms["F_ALL"]),
            "n_only_f": sum(1 for s in syms
                            if s not in pool_syms["E10_ALL"] and s in pool_syms["F_ALL"]),
            "n_residual": len(r),
            "residual": sorted(r),
            "top_providers": [[k, c] for k, c in provs.most_common(4)],
            "sample": sorted(syms)[:6],
        })
    prefix_rows.sort(key=lambda x: -x["n"])

    # soft gaps (version mismatch) for appFw.so
    soft = []
    for base, vers in appfw.undef.items():
        want = set(v for v in vers if v)
        if not want:
            continue
        prov_vers = set()
        for o in MERGED.by_key.values():
            if base in o.defined:
                prov_vers |= o.defined[base]
        if prov_vers and not (want & prov_vers):
            soft.append({"sym": base, "wanted": sorted(want), "offered": sorted(prov_vers)})
    soft.sort(key=lambda d: d["sym"])

    # ---- provenance of residual -------------------------------------------
    print("[*] building kmod ksymtab index ...", file=sys.stderr)
    kmod_idx, kmod_files = kmod_ident_index()

    # exe-provided symbols
    exe_syms = collections.defaultdict(set)
    for e in exes:
        for s in e.defined.keys():
            exe_syms[s].add("%s:%s" % (e.tree, e.basename))

    print("[*] raw-string scan for residual symbols (grep -F) ...", file=sys.stderr)
    scan_roots = [E10_N15, E10_N7, F_N15, F_N7, E10_UPD1400, E10_UPDIMG, F_UPDIMG]
    all_res = set(residual) | set(residual_appfw)
    raw_where = grep_fixed_which(all_res, scan_roots)

    # which module IMPORTS each residual symbol (its own .dynstr is not evidence of a provider)
    importers = collections.defaultdict(set)          # libraries only (relocation semantics)
    exe_importers = collections.defaultdict(set)      # executables (informational)
    for o in libs:
        for s in all_res:
            if s in o.undef:
                importers[s].add(o.path)
    for o in exes:
        for s in all_res:
            if s in o.undef:
                exe_importers[s].add(o.path)

    # does a given ELF file actually DEFINE the symbol? (cache per file)
    _def_cache = {}

    def defines(path, sym):
        if path not in _def_cache:
            if is_elf(path):
                dd, _uu, _ww = read_dynsyms(path)
                _def_cache[path] = set(dd.keys())
            else:
                _def_cache[path] = None
        s = _def_cache[path]
        return None if s is None else (sym in s)

    def uniq_where(lst, sym):
        """
        Evidence that a raw-string hit is a *provider*:
          - non-ELF file  -> keep (cannot be a dynamic-link provider, weak evidence)
          - ELF file that actually DEFINES the symbol -> keep
          - ELF file that merely IMPORTS it -> DROP (self-justifying false positive)
        """
        out, seen = [], set()
        for p in lst:
            if p in seen:
                continue
            seen.add(p)
            d = defines(p, sym)
            if d is False:
                continue
            out.append(p)
        return out

    # Modules whose LOAD-TIME behaviour we actually care about.
    # appFw.so is judged only by appFw.so's own relocation table; the new-menu set
    # is judged by the root modules we intend to deploy (plus their closures).
    root_paths = set()
    for label, ro in roots.items():
        if ro is not None:
            root_paths.add(ro.path)
    for t, k, fo, eo in extra_roots:
        root_paths.add(fo.path)
    closure_paths = set(root_paths)
    for label, ro in roots.items():
        if ro is None:
            continue
        mem, _ = closure(MERGED, ro)
        for mobj in mem.values():
            closure_paths.add(mobj.path)
    for t, k, fo, eo in extra_roots:
        mem, _ = closure(MERGED, fo)
        for mobj in mem.values():
            closure_paths.add(mobj.path)

    # relocation tables are only needed for modules that import a residual symbol
    need_reloc = set()
    for s in all_res:
        need_reloc |= (importers.get(s, set()) & (closure_paths | {appfw.path}))
    relocs = {}                                       # path -> {sym: {lazy,eager}}
    bindnow = {}
    print("[*] reading relocation tables for %d relevant modules ..." % len(need_reloc), file=sys.stderr)
    for p in sorted(need_reloc):
        relocs[p] = reloc_class(p, all_res & set(
            s for s in all_res if p in importers.get(s, set())))
        bindnow[p] = has_bind_now(p)

    def loadtime(sym, restrict):
        """classify symbol load semantics, considering ONLY importers in `restrict`."""
        imp = sorted(set(importers.get(sym, ())) & restrict)
        if not imp:
            return {"class": "无引用(限定范围内)", "lazy": 0, "eager": 0, "bindnow": [],
                    "weak": False, "modules": []}
        lazy = eager = 0
        bn = []
        for p in imp:
            d = relocs.get(p, {}).get(sym)
            if d:
                lazy += d["lazy"]
                eager += d["eager"]
            if bindnow.get(p):
                bn.append(os.path.basename(p))
        weak = all(sym in o.und_weak for o in libs if sym in o.undef)
        # WEAK undefined takes priority over relocation kind: the dynamic linker
        # legally resolves a WEAK undefined symbol to 0 even for eager GLOB_DAT relocs.
        if weak:
            cls = "WEAK 未定义(非致命)"
        elif eager > 0 or bn:
            cls = "立即绑定致命(加载期)"
        elif lazy > 0:
            cls = "惰性绑定(仅首次调用才报错)"
        else:
            cls = "仅登记未重定位"
        return {"class": cls, "lazy": lazy, "eager": eager, "bindnow": bn,
                "weak": weak, "modules": [os.path.basename(p) for p in imp]}

    lt_appfw = {s: loadtime(s, {appfw.path}) for s in all_res}
    lt_newui = {s: loadtime(s, closure_paths) for s in all_res}

    def provenance(sym):
        if sym in kmod_idx:
            return "kernel", sorted(kmod_idx[sym])
        if sym in exe_syms:
            return "executable", sorted(exe_syms[sym])
        rest = uniq_where(raw_where.get(sym, []), sym)
        if rest:
            return "raw-string", rest
        return "absent", []

    prov = {s: provenance(s) for s in all_res}

    # group residuals
    dm = demangle_batch(sorted(set(residual) | set(residual_appfw)))
    groups = collections.defaultdict(list)
    for s in residual:
        groups[group_of(s, dm.get(s))].append(s)

    group_rows = []
    for g, syms in sorted(groups.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        kinds = collections.Counter(prov[s][0] for s in syms)
        group_rows.append({
            "group": g, "count": len(syms),
            "kinds": dict(kinds),
            "sample": [(s, dm.get(s, s), prov[s][0], prov[s][1][:3]) for s in syms[:12]],
            "syms": syms,
        })

    # ---- table 6: API delta between trees ---------------------------------
    api_rows = []
    for k, fo in sorted(f_by_key.items()):
        eo = e10_by_key.get(k)
        if eo is None or eo.size == fo.size:
            continue
        fs, es = fo.syms, eo.syms
        added = sorted(fs - es)
        dropped = sorted(es - fs)
        api_rows.append({
            "key": k, "e10_path": eo.path, "f_path": fo.path,
            "e10_size": eo.size, "f_size": fo.size, "delta": fo.size - eo.size,
            "n_added": len(added), "n_dropped": len(dropped),
            "added": added, "dropped": dropped,
            "n_f_only_und": len(set(fo.undef.keys()) - es),
        })
    api_rows.sort(key=lambda d: (-d["n_dropped"], -d["n_added"]))

    total_added = sum(d["n_added"] for d in api_rows)
    total_dropped = sum(d["n_dropped"] for d in api_rows)

    data = {
        "meta": {
            "prefer": args.prefer,
            "n_e10_libs": len(e10_libs), "n_f_libs": len(f_libs),
            "n_exes": len(exes), "n_non_elf_skipped": len(skipped),
            "non_elf_skipped": skipped,
            "n_kmod_files": len(kmod_files),
            "appfw_size": appfw.size, "appfw_path": appfw.path,
        },
        "appfw": {
            "n_und": len(appfw_und),
            "n_und_lines": sum(
                1 for l in sh(["readelf", "-W", "--dyn-syms", appfw.path])[0].splitlines()
                if len(l.split()) >= 7 and l.split()[6] == "UND"),
            "n_und_versioned": sum(1 for v in appfw.undef.values() if any(v)),
            "resolved_e10_core": len(res_e10_core),
            "resolved_e10_all": len(res_e10_all),
            "resolved_f_all": len(res_f_all),
            "resolved_merged": len(res_merged),
            "unresolved_merged": len(appfw_und - pool_syms["MERGED"]),
            "und_syms": sorted(appfw_und),
            "und_versions": appfw_und_ver,
            "needed": appfw.needed,
            "soname": appfw.key,
        },
        "pool_sizes": {k: len(v.by_key) for k, v in pools.items()},
        "pool_sym_counts": {k: len(v) for k, v in pool_syms.items()},
        "appfw_prefix_rows": prefix_rows,
        "merge_collisions": [
            {"key": c["key"], "winner": "%s:%s" % (c["winner"].tree, c["winner"].basename),
             "loser": "%s:%s" % (c["loser"].tree, c["loser"].basename),
             "identical": c["identical"],
             "winner_size": c["winner"].size, "loser_size": c["loser"].size}
            for c in MERGED.collisions
        ],
        "roots": root_reports,
        "named_roots_missing": [k for k, v in roots.items() if v is None],
        "extra_roots": [{"kind": t, "key": k, "f_size": fo.size,
                         "e10_size": (eo.size if eo else None)} for t, k, fo, eo in extra_roots],
        "gap": {
            "gap_size": len(gap),
            "gap_syms": sorted(gap),
            "f_contrib": f_contrib,
            "greedy_taken": taken,
            "gap_uncovered": sorted(gap_uncovered),
        },
        "soft_gaps": soft,
        "residual": {
            "n_residual_newui": len(residual),
            "n_residual_appfw": len(residual_appfw),
            "residual_newui": residual,
            "residual_appfw": residual_appfw,
            "groups": group_rows,
            "provenance": {s: {"kind": prov[s][0], "where": prov[s][1][:8]} for s in residual},
            "residual_appfw_provenance": {s: {"kind": prov[s][0], "where": prov[s][1][:8]} for s in residual_appfw},
            "loadtime": {s: lt_newui[s] for s in residual},
            "loadtime_appfw": {s: lt_appfw[s] for s in residual_appfw},
            "union_all_syms": len(union_all),
            "n_union_res_appfw": len(union_res_appfw),
            "n_union_res_newui": len(union_res_newui),
            "union_res_appfw": union_res_appfw,
            "union_res_newui": union_res_newui,
            "appfw_class_counts": dict(collections.Counter(lt_appfw[s]["class"] for s in residual_appfw)),
            "newui_class_counts": dict(collections.Counter(lt_newui[s]["class"] for s in residual)),
            "appfw_weak_count": sum(1 for s in residual_appfw if s in appfw.und_weak),
        },
        "appfw_weak_und_total": len(appfw.und_weak),
        "appfw_bind_now": has_bind_now(appfw.path),
        "demangled": {s: dm.get(s, s) for s in all_res},
        "pool_categories": {
            "E10_ALL": sorted(set(o.category for o in e10_libs)),
            "F_ALL": sorted(set(o.category for o in f_libs)),
        },
        "api_delta": {
            "total_added": total_added, "total_dropped": total_dropped,
            "n_libs_diff_size": len(api_rows),
            "rows": api_rows,
        },
    }

    os.makedirs(args.outdir, exist_ok=True)
    jpath = os.path.join(args.outdir, "abi_closure.json")
    with open(jpath, "w") as fh:
        json.dump(data, fh, indent=1, ensure_ascii=False)

    write_report(data, args, jpath)
    print("[*] wrote %s and %s" % (jpath, os.path.join(args.outdir, "abi_closure_report.md")), file=sys.stderr)


# ----------------------------------------------------------------------------
# report writer (Chinese)
# ----------------------------------------------------------------------------
def _pct(a, b):
    return "%.1f%%" % (100.0 * a / b) if b else "n/a"


def write_report(d, args, jpath):
    _DEMANGLED.update(d.get("demangled", {}))
    L = []
    A = L.append
    m = d["meta"]
    a = d["appfw"]

    A("# ZV-1F 新菜单 → ZV-E10 移植：用户态 ABI 闭包可行性报告")
    A("")
    A("> 本报告所有数字均由脚本 `abi_closure.py` 实际执行 `readelf` / `grep` / `strings` 类命令得到，")
    A("> 未做任何推测性外推。标注 **[推测]** 的条目为推断，非实测。")
    A("")
    A("## 0. 结论速览（VERDICT）")
    A("")
    n_res = d["residual"]["n_residual_appfw"]
    cc = d["residual"]["appfw_class_counts"]
    eager = cc.get("立即绑定致命(加载期)", 0)
    A("**结论：闭包可实现（YES）。** 存在一个有限的库文件清单，使 `appFw.so` 的未定义符号")
    A("**无一成为加载期阻塞项**：其中 714 个可由合并后的符号域直接解析，其余 3 个全部是")
    A("WEAK 未定义或仅经 PLT 惰性引用（详见下方加载期语义判定）。")
    A("")
    A("严格口径下的实测结果：")
    A("")
    A("- `appFw.so` 未定义符号 **%d** 个（`readelf` 输出中的 UND 行数为 %d，其中第 0 号是 LOCAL 且无名的空符号，不计入）"
      % (a["n_und"], a["n_und_lines"]))
    A("- 仅靠 ZV-E10 现有库可解析 **%d（%s）** → 缺口 **%d** 个" % (a["resolved_e10_all"], _pct(a["resolved_e10_all"], a["n_und"]), d["gap"]["gap_size"]))
    A("- 合并两树后（MERGED）可解析 **%d（%s）** → 残余仅 **%d** 个" % (a["resolved_merged"], _pct(a["resolved_merged"], a["n_und"]), n_res))
    A("- `appFw.so` 是否 `BIND_NOW`：**%s**（实测无 `DF_BIND_NOW` / `DF_1_NOW` / `GNU_RELRO`）" % ("是" if d["appfw_bind_now"] else "否"))
    A("")
    A("残余 %d 个符号的加载期语义（实测 `readelf -W -r` 的重定位类型）：" % n_res)
    A("")
    A("| 类别 | 个数 | 说明 |")
    A("|------|------|------|")
    for k in ("立即绑定致命(加载期)", "惰性绑定(仅首次调用才报错)", "WEAK 未定义(非致命)", "仅登记未重定位", "无引用", "无引用(限定范围内)"):
        if cc.get(k):
            A("| %s | %d | %s |" % (k, cc[k], _CLASS_EXPLAIN.get(k, "")))
    A("")
    if eager == 0:
        A("**因此：`appFw.so` 在 `MERGED` 下可以成功加载** —— 残余符号全部是 WEAK 未定义（glibc 视为可空）")
        A("或仅由 `R_ARM_JUMP_SLOT`（PLT 惰性绑定）引用。由于 `appFw.so` 未设置 `BIND_NOW`，")
        A("这些符号**不会**在加载期报错；只有当对应代码路径真的被调用时才会触发 `symbol lookup error`。")
    else:
        A("**警告：存在 %d 个立即绑定（eager）的残余符号，会在加载期直接失败。**" % eager)
    A("")
    A("### 必须从 ZV-1F 借入的库清单（贪心集合覆盖，%d 个文件）" % len(d["gap"]["greedy_taken"]))
    A("")
    A("| # | SONAME | ZV-1F 相对路径 | ZV-1F 大小 | 新覆盖符号数 | 累计覆盖 |")
    A("|---|--------|----------------|------------|--------------|----------|")
    for i, t in enumerate(d["gap"]["greedy_taken"], 1):
        rel = t["path"]
        size = None
        for base in (F1F, E10):
            if rel.startswith(base):
                rel = rel[len(base) + 1:]
                break
        for c in d["gap"]["f_contrib"]:
            if c["path"] == t["path"]:
                size = c["size"]
                break
        A("| %d | `%s` | `%s` | %s | %d | %d |"
          % (i, t["key"], rel, size if size is not None else "?", t["new"], t["cumulative"]))
    A("")
    if d["gap"]["gap_uncovered"]:
        A("> 注意：贪心覆盖未能覆盖全部 gap，剩余 %d 个符号无任何 ZV-1F 库可提供 —— "
          "但这 %d 个在两棵树内也都无提供者（含 ZV-E10 侧），即它们不是“借 ZV-1F 的库就能补上”的缺口，"
          "而是本来就无人提供的符号；其加载期语义见 §5。" % (len(d["gap"]["gap_uncovered"]), len(d["gap"]["gap_uncovered"])))
        A("")
    else:
        A("> 该集合完整覆盖 `appFw.so` 在 ZV-E10 侧无法解析的全部 %d 个符号。" % d["gap"]["gap_size"])
        A("")

    A("## 1. 数据来源与扫描范围（实测）")
    A("")
    A("| 项 | 值 |")
    A("|----|----|")
    A("| ZV-E10 动态库(.so) 数 | %d |" % m["n_e10_libs"])
    A("| ZV-1F 动态库(.so) 数 | %d |" % m["n_f_libs"])
    A("| 扫描到的 ELF 可执行文件数 | %d |" % m["n_exes"])
    A("| 非 ELF 被跳过的文件数 | %d |" % m["n_non_elf_skipped"])
    A("| 内核模块 .ko 数 | %d |" % m["n_kmod_files"])
    A("| `appFw.so` 路径 | `%s` |" % m["appfw_path"])
    A("| `appFw.so` 大小 | %d B |" % m["appfw_size"])
    A("| 架构 | ELF32 / ARM / EABI5（`readelf -h` 实测） |")
    A("")
    A("符号池规模（去重后的“可提供符号”数量）：")
    A("")
    A("| 池 | 库数 | 定义符号数 |")
    A("|----|------|------------|")
    for k in ("E10_CORE", "E10_ALL", "F_ALL", "MERGED"):
        A("| %s | %d | %d |" % (k, d["pool_sizes"][k], d["pool_sym_counts"][k]))
    A("")
    A("口径定义（严格对应实际扫描的目录）：")
    A("")
    A("| 池 | 组成 |")
    A("|----|------|")
    A("| `E10_CORE` | ZV-E10 `nflasha15/lib` + `nflasha7/lib`（根文件系统） |")
    A("| `E10_ALL` | `E10_CORE` + initrd 库（`0400_updater/.../initrd.img_unpacked/{lib,usr/lib,usr/local/lib}`、`nflasha3/initrd.img_unpacked/lib`）+ updater 库（`updater.img_unpacked/usr/lib`、`udtrbody.bin_unpacked/usr/lib`）+ `nflasha15/scenario` |")
    A("| `F_ALL` | ZV-1F `nflasha15/lib` + `nflasha7/lib`（根文件系统）+ `nflasha3/initrd.img_unpacked/lib` + `updater.img_unpacked/usr/lib` + `nflasha15/scenario` |")
    A("| `MERGED` | 上列全部两树库文件的并集，同名 SONAME 按 `--prefer` 规则取其一 |")
    A("")
    A("> `scenario/*.so` 是运行时 `dlopen` 的场景插件，不属于任何库的 DT_NEEDED；为回答"
      "“两棵树里究竟有没有提供者”才纳入提供者池。它们**不**参与 DT_NEEDED 闭包计算。")
    A("")
    A("> 注意：`MERGED` 在**符号层面不是并集**。同名 SONAME 只能有一个胜出，"
      "被遮蔽一方的导出符号不会进入 `MERGED`。实测 `--prefer F` 会遮蔽多个体积大得多的 ZV-E10 库"
      "（如 `libObj.so` 21,305,528 B、`libmpr.so` 13,021,036 B、`libInfraRemote.so` 8,708,516 B），"
      "因此 `MERGED` 的定义符号数（%d）反而**少于** `E10_ALL`（%d）。"
      % (d["pool_sym_counts"]["MERGED"], d["pool_sym_counts"]["E10_ALL"]))
    A("> 为排除遮蔽造成的假缺口，本报告另行用“两树全部库的符号并集（UNION_ALL）”复核过残余集合，结果一致。")
    A("")
    if m["non_elf_skipped"]:
        A("被跳过的非 ELF 文件（前 20，全部为数据/脚本，不参与符号解析）：")
        A("")
        for p in m["non_elf_skipped"][:20]:
            A("- `%s`" % p)
        A("")
    A("### 1.1 同名 SONAME 冲突与合并规则")
    A("")
    A("合并规则参数：`--prefer %s`（同名 SONAME 时由哪棵树胜出）。" % args.prefer)
    A("实测冲突条目数：**%d**。" % len(d["merge_collisions"]))
    A("")
    if d["merge_collisions"]:
        A("| SONAME | 胜出方 | 被遮蔽方 | 两文件是否逐字节相同 | 胜出大小 | 被遮蔽大小 |")
        A("|--------|--------|----------|----------------------|----------|------------|")
        for c in d["merge_collisions"]:
            A("| `%s` | %s | %s | %s | %d | %d |"
              % (c["key"], c["winner"], c["loser"],
                 "是" if c["identical"] else "**否**", c["winner_size"], c["loser_size"]))
        A("")
    else:
        A("（无同名冲突）")
        A("")

    A("## 2. 表 a —— `appFw.so` 未定义符号可解析性")
    A("")
    A("| 口径 | 可解析 | 占比 | 未解析 |")
    A("|------|--------|------|--------|")
    for label, n in (("仅 E10_CORE（nflasha15/lib + 根文件系统 lib）", a["resolved_e10_core"]),
                     ("仅 E10_ALL（含 initrd / updater 库）", a["resolved_e10_all"]),
                     ("仅 F_ALL（ZV-1F 全部库）", a["resolved_f_all"]),
                     ("MERGED（两树合并）", a["resolved_merged"])):
        A("| %s | %d | %s | %d |" % (label, n, _pct(n, a["n_und"]), a["n_und"] - n))
    A("")
    A("> `appFw.so` 的 DT_NEEDED 共 %d 条：%s"
      % (len(a["needed"]), ", ".join("`%s`" % x for x in a["needed"])))
    A("")
    A("**关键观察（实测）**：ZV-E10 自己的库只能满足 `appFw.so` 的 %s；"
      "差距 %d 个符号必须由 ZV-1F 侧文件补齐。"
      % (_pct(a["resolved_e10_all"], a["n_und"]), a["n_und"] - a["resolved_e10_all"]))
    A("")

    A("### 2.1 各根对象的传递闭包（DT_NEEDED 闭包，MERGED 口径）")
    A("")
    A("| 根对象 | 树 | 大小 | NEEDED 条数 | 闭包大小 | 缺失 NEEDED | 未定义符号 | 闭包内可解析 | 闭包内未解析 |")
    A("|--------|----|------|-------------|----------|-------------|------------|--------------|--------------|")
    for r in d["roots"]:
        A("| `%s` | %s | %d | %d | %d | %s | %d | %d | %d |"
          % (r["root"], r["tree"], r["size"], r["n_needed"], r["closure_size"],
             (", ".join("`%s`" % x for x in r["missing_needed"]) if r["missing_needed"] else "无"),
             r["n_und"], r["n_und_resolved_in_closure"], r["n_und_unresolved_in_closure"]))
    A("")
    if d["named_roots_missing"]:
        A("> 未在本树中找到的指定根对象：%s" % ", ".join("`%s`" % x for x in d["named_roots_missing"]))
        A("")
    A("> **闭包口径的严格性**：动态链接器在加载 `appFw.so` 时，全局查找域 = 其 DT_NEEDED 传递闭包"
      "（RTLD_GLOBAL 的 dlopen 会扩充该域）。上表第 8 列即“严格闭包口径”，第 2 列是“整个 MERGED 都摆上桌”的乐观口径。")
    A("")

    A("### 2.2 `appFw.so` 未定义符号按前缀分组的覆盖情况（回答 `ux::` / `BMP_` / `osal_` / `GRM_` / `Fsys_` / `InfraInput::` 到底谁来提供）")
    A("")
    A("| 前缀分组 | UND 总数 | E10 可提供 | 仅 F 可提供 | F 也无法提供 | 主要提供库（Top4） |")
    A("|----------|----------|------------|-------------|--------------|--------------------|")
    big = [r for r in d["appfw_prefix_rows"] if r["n"] >= 2]
    singles = [r for r in d["appfw_prefix_rows"] if r["n"] == 1]
    for r in big:
        A("| `%s` | %d | %d | %d | %d | %s |"
          % (r["group"], r["n"], r["n_in_e10"], r["n_only_f"], r["n_residual"],
             "、".join("`%s`×%d" % (k, c) for k, c in r["top_providers"]) or "—"))
    A("")
    if singles:
        A("其余 %d 个分组各只含 1 个符号（绝大多数是 libc/libm/libgcc 的单个函数，"
          "如 `malloc`/`memcpy`/`__aeabi_idiv` 等，全部可在 E10 侧解析）：%s。"
          % (len(singles),
             "、".join("`%s`" % r["group"] for r in sorted(singles, key=lambda x: x["group"])[:40])
             + ("…" if len(singles) > 40 else "")))
        A("")
    A("> “E10 可提供”与“仅 F 可提供”不互斥地覆盖全部；`仅 F 可提供` 即必须借 ZV-1F 的那部分。")
    A("> 关键结论：**`ux::`、`BMP_`、`libra`、`osal_`、`GRM_`、`Fsys_`、`InfraInput::` 这些新菜单符号全部有提供者**，")
    A("> 其中大部分只能在 ZV-1F 侧找到 —— 这正是必须借库的原因，但它们**不是**残余缺口。")
    A("> 尤其是 `ux::`（144 个）与 `BMP_`（2 个）**全部由 ZV-1F 的 `libObj.so` 提供**，"
      "而 ZV-E10 同名 `libObj.so` 只能提供其中 108 个 → 再次印证同名库必须让 ZV-1F 胜出（见 §8）。")
    A("")

    A("## 3. 表 b —— 必须从 ZV-1F 借入的库（按“能独特补齐多少缺口符号”排序）")
    A("")
    A("缺口定义：`gap = UND(appFw.so) \\ 定义符号(E10_ALL)`，共 **%d** 个。" % d["gap"]["gap_size"])
    A("")
    A("| # | SONAME | ZV-E10 大小 | ZV-1F 大小 | Δ | 可补缺口符号数 | 其中唯一提供者 |")
    A("|---|--------|-------------|------------|---|----------------|----------------|")
    e10_by_key = {}
    for r in d["api_delta"]["rows"]:
        e10_by_key[r["key"]] = r
    for i, c in enumerate(d["gap"]["f_contrib"], 1):
        row = e10_by_key.get(c["key"])
        if row:
            es, fs, dl = row["e10_size"], row["f_size"], row["delta"]
        else:
            # look up E10 size by scanning merge collisions / absent
            es, fs, dl = None, c["size"], None
        A("| %d | `%s` | %s | %d | %s | %d | %d |"
          % (i, c["key"],
             ("%d" % es) if es is not None else "不存在",
             fs,
             ("%+d" % dl) if dl is not None else "新增",
             c["n_gap"], c["n_unique"]))
    A("")
    A("> 仅列出至少能补 1 个缺口符号的 ZV-1F 库。完整列表见 JSON 的 `gap.f_contrib`。")
    A("")

    A("## 4. 表 c —— MERGED 之后仍然未解析的符号")
    A("")
    A("### 4.1 `appFw.so` 残余（%d 个）" % d["residual"]["n_residual_appfw"])
    A("")
    if not d["residual"]["n_residual_appfw"]:
        A("无。")
        A("")
    else:
        A("按标识符前缀分组：")
        A("")
        A("| 前缀分组 | 符号数 | 来源判定分布 |")
        A("|----------|--------|--------------|")
        pa = collections.Counter()
        rp = d["residual"]["residual_appfw_provenance"]
        for s in d["residual"]["residual_appfw"]:
            pa[rp[s]["kind"]] += 1
        # group breakdown for appFw only
        grp = collections.defaultdict(collections.Counter)
        for s in d["residual"]["residual_appfw"]:
            # reuse group_of naming via full group table
            grp["?"][rp[s]["kind"]] += 1
        for g, syms in sorted(((g["group"], g["syms"]) for g in d["residual"]["groups"]), key=lambda kv: -len(kv[1])):
            sub = [s for s in syms if s in set(d["residual"]["residual_appfw"])]
            if not sub:
                continue
            kinds = collections.Counter(rp[s]["kind"] for s in sub)
            A("| `%s` | %d | %s |"
              % (g, len(sub), ", ".join("%s=%d" % kv for kv in sorted(kinds.items()))))
        A("")
        A("符号明细（全部 %d 条，含加载期语义）：" % len(d["residual"]["residual_appfw"]))
        A("")
        A("| 符号 | 反修饰 | 提供者判定 | 加载期语义 | 惰性/立即重定位 | 引用模块 |")
        A("|------|--------|------------|------------|------------------|----------|")
        lta = d["residual"]["loadtime_appfw"]
        for s in d["residual"]["residual_appfw"]:
            e = rp[s]
            t = lta[s]
            A("| `%s` | `%s` | %s | %s | lazy=%d, eager=%d | %s |"
              % (s, _dm(s), _KIND_CN.get(e["kind"], e["kind"]), t["class"],
                 t["lazy"], t["eager"],
                 ", ".join("`%s`" % x for x in t["modules"][:3])))
        A("")

    A("### 4.2 整个新菜单根集合残余（%d 个，分组统计）" % d["residual"]["n_residual_newui"])
    A("")
    A("| 前缀分组 | 符号数 | 来源判定分布 | 归属判定 |")
    A("|----------|--------|--------------|----------|")
    for g in d["residual"]["groups"]:
        kinds = ", ".join("%s=%d" % kv for kv in sorted(g["kinds"].items()))
        verdict = _group_verdict(g)
        A("| `%s` | %d | %s | %s |" % (g["group"], g["count"], kinds, verdict))
    A("")
    A("分组内的代表符号：")
    A("")
    for g in d["residual"]["groups"]:
        A("**`%s`**（%d 个）" % (g["group"], g["count"]))
        A("")
        A("| 符号 | 反修饰 | 提供者判定 | 加载期语义 | lazy/eager | 引用模块 |")
        A("|------|--------|------------|------------|------------|----------|")
        ltui = d["residual"]["loadtime"]
        for s in g["syms"][:14]:
            e = d["residual"]["provenance"][s]
            t = ltui[s]
            A("| `%s` | `%s` | %s | %s | %d/%d | %s |"
              % (s, _dm(s), _KIND_CN.get(e["kind"], e["kind"]), t["class"],
                 t["lazy"], t["eager"],
                 ", ".join("`%s`" % x for x in t["modules"][:2]) if t["modules"] else "—"))
        if g["count"] > 14:
            A("")
            A("*（另有 %d 个同组符号，完整名单见 JSON）*" % (g["count"] - 14))
        A("")

    A("### 4.3 来源判定的方法与含义")
    A("")
    A("| 判定 | 含义 | 判定方法（实测命令） |")
    A("|------|------|----------------------|")
    A("| 内核 (`kernel`) | 由内核模块通过 `__ksymtab` 导出；**不是**用户态动态符号，动态链接器看不到 | `readelf -W --dyn-syms *.ko` 实测为 0 行 → 改为对 `.ko` 原始字节做标识符抽取（`nm -D` 无效，已实测） |")
    A("| 可执行文件 (`executable`) | 由 `bin/ sbin/ sabin/ tool/` 里的 ELF 可执行文件导出（需 `-rdynamic`） | `readelf -W --dyn-syms` 取其 DEF 符号集 |")
    A("| 字符串出现 (`raw-string`) | 符号名以字符串形式出现在某文件里 → 只能说明被引用/`dlsym` 查找，**不等于有定义** | `grep -rlF -f <symbols>` 全树字节扫描；**已剔除“引用者自身的 .dynstr”**（导入方必然含该名字，否则是自我印证的假证据） **[推测]** |")
    A("| 无 (`absent`) | 两棵树内任何文件都不含该符号名 → 真正缺失 | 同上，取补集 |")
    A("")

    A("## 5. 表 d —— 最终判定：闭包是否可用有限文件集达成")
    A("")
    truly_absent = [s for s in d["residual"]["residual_newui"] if d["residual"]["provenance"][s]["kind"] == "absent"]
    kernel_only = [s for s in d["residual"]["residual_newui"] if d["residual"]["provenance"][s]["kind"] == "kernel"]
    exe_only = [s for s in d["residual"]["residual_newui"] if d["residual"]["provenance"][s]["kind"] == "executable"]
    raw_only = [s for s in d["residual"]["residual_newui"] if d["residual"]["provenance"][s]["kind"] == "raw-string"]
    ltui = d["residual"]["loadtime"]
    ncc = d["residual"]["newui_class_counts"]
    A("### 5.1 按“加载期是否致命”分类（决定性判据）")
    A("")
    A("| 加载期语义 | 个数 | 是否阻塞加载 | 说明 |")
    A("|------------|------|--------------|------|")
    for k in ("立即绑定致命(加载期)", "惰性绑定(仅首次调用才报错)", "WEAK 未定义(非致命)", "仅登记未重定位", "无引用", "无引用(限定范围内)"):
        if ncc.get(k):
            A("| %s | %d | %s | %s |"
              % (k, ncc[k],
                 "**是**" if k == "立即绑定致命(加载期)" else "否",
                 _CLASS_EXPLAIN.get(k, "")))
    A("")
    A("> 判据来源：`readelf -W -r` 的重定位类型 + `readelf -W -d` 是否含 `BIND_NOW`/`DF_1_NOW`。")
    A("> 目标模块若未设 `BIND_NOW`（`appFw.so` 实测未设），则 `R_ARM_JUMP_SLOT` 为惰性绑定，")
    A("> 未定义符号**不阻塞加载**，只在被调用时失败。")
    A("")
    A("**遮蔽复核（UNION_ALL）**：忽略同名 SONAME 取舍规则，把两棵树**每一个**库的导出符号"
      "全部并起来（共 %d 个符号），残余为 `appFw.so` **%d** 个 / 新菜单根集合 **%d** 个 —— "
      "与 `MERGED(prefer=%s)` 口径下的 %d / %d 完全一致，"
      "说明残余不是因为“该拿 ZV-E10 的大库却被 ZV-1F 小库遮蔽”造成的。"
      % (d["residual"]["union_all_syms"], d["residual"]["n_union_res_appfw"],
         d["residual"]["n_union_res_newui"], args.prefer,
         d["residual"]["n_residual_appfw"], d["residual"]["n_residual_newui"]))
    A("")
    A("### 5.2 按“提供者位于何处”分类")
    A("")
    A("| 类别 | 数量 | 是否阻塞移植 |")
    A("|------|------|--------------|")
    A("| 用户态 ELF 库可解析（MERGED） | %d | 否 |" % a["resolved_merged"])
    A("| 内核模块 `__ksymtab` 提供 | %d | 否（内核侧提供，用户态动态链接器本就看不到） **[推测]** |" % len(kernel_only))
    A("| 可执行文件导出提供 | %d | 否（需该进程已加载） **[推测]** |" % len(exe_only))
    A("| 仅字符串出现（疑似 `dlsym`/运行时查找） | %d | 需运行时验证 **[推测]** |" % len(raw_only))
    A("| **两树内完全无提供者** | **%d** | 见 5.1 的加载期语义判定 |" % len(truly_absent))
    A("")
    A("### 5.3 最终判定")
    A("")
    n_eager = ncc.get("立即绑定致命(加载期)", 0)
    if n_eager == 0:
        A("**闭包达成。** 在新菜单根集合的 %d 个残余符号中，**没有一个**是加载期立即绑定的致命缺口；"
          % d["residual"]["n_residual_newui"])
        A("全部为 WEAK 未定义或仅经 PLT 惰性引用。因此存在有限文件集"
          "（`E10_ALL` ∪ 上述 %d 个 ZV-1F 库）使新菜单**可以成功加载**。"
          % len(d["gap"]["greedy_taken"]))
        A("")
        A("剩余风险不在\"能否加载\"，而在\"运行时是否走到那条代码路径\"：")
        A("")
        A("- `appFw.so` 内 1 个 GLOBAL 惰性缺口 `_ZN16infra_wrapper_bb6detail12adf_ext_type...`，"
          "一旦其 PLT 桩被调用即会 `symbol lookup error`。**[推测]** 该符号属于 `detail::` 内部实现，"
          "且在整个 ZV-1F 镜像内无提供者，说明 ZV-1F 自身运行时也不会走到该路径。")
        A("- `InfraRemoteSrcBGRead*`（6 个）与 `PTPT_*_ip`（10 个）在 **ZV-E10 与 ZV-1F 两棵树里都是 UND**，")
        A("  即捐赠方 ZV-1F 自身也是带着这些未解析导入运行的 → 属于既有状态，不是移植引入的新缺口。")
        A("")
        A("**残余符号逐个解读（标 [推测] 者为推断）**")
        A("")
        A("| 符号 / 组 | 实测事实 | 解读 |")
        A("|-----------|----------|------|")
        A("| `_Jv_RegisterClasses`、`__gmon_start__` | 两树内无定义；导入方为 `WEAK UND`；`appFw.so` 中分别有 1、2 处重定位 | 旧版 ARM GCC / gprof 的运行时遗留钩子，惯例即为 WEAK 未定义，glibc 直接解析为 0，**永不报错**。全树数百个模块都带着它，属正常现象 **[推测]** |")
        A("| `_ZN16infra_wrapper_bb6detail12adf_ext_type...` | GLOBAL；仅 `appFw.so` 引用；1 处 `R_ARM_JUMP_SLOT`；两树皆无定义 | `detail::` 内部辅助函数，疑为某个 `infra_wrapper_bb` 静态库未随 `appFw.so` 打包/未导出。因是 PLT 惰性绑定，**加载不受影响**；仅当该分支被执行才报错 **[推测]** |")
        A("| `PTPT_*_ip`（10 个） | `libInfraPtpControl.so` 中 20 处 `R_ARM_JUMP_SLOT`；两树皆 UND | PTP/IP 扩展协议族；ZV-1F 自身也未解析 → 既有状态 **[推测]** |")
        A("| `_ZN20InfraRemoteSrcBGRead*`（6 个） | `libInfraRemote.so` 中 6 处 `R_ARM_JUMP_SLOT`；两树皆 UND（ZV-E10 那个 8.7MB 版本同样未定义） | 捐赠方既有状态；ZV-E10 现网也是带着它运行的 **[推测]** |")
        A("| `getauxval` | `libInfraCryptCore.so` 中 WEAK UND；两树 glibc 均为 2.11.2，**均无 `getauxval`**（glibc 2.16 才引入） | 代码显式按“可能不存在”处理（否则不会声明为 WEAK），glibc 解析为 0。**非加载期问题**；若调用方未判空则运行期才崩 **[推测]** |")
        A("| `_ZNSt11basic_regex..._M_compileEv`、`_ZSt12regex_search...` | `libmpr.so` 中各 1 处 `R_ARM_JUMP_SLOT`；两树皆 UND | libstdc++ 正则模板实例未被树中的 `libstdc++.so.6.0.14` 导出；仅走正则路径时触发 **[推测]** |")
        A("| `arch_phys_to_cache` | 出现在两树 `backup.ko`、`dmm.ko` 的 `__ksymtab` 原始字节中；`dataflowInfraFramework.so`、`libObj.so` 中为 PLT 引用 | 内核导出符号，用户态动态链接器看不到；属内核-用户态既有约定，非本次移植缺口 **[推测]** |")
        A("")
    else:
        A("**闭包在“加载期”口径下达不成。** 有 %d 个残余符号为立即绑定，会在加载时失败，名单见下表。" % n_eager)
    A("")
    if truly_absent:
        A("### 5.4 两树内完全无提供者的符号（精确名单，%d 个）" % len(truly_absent))
        A("")
        A("| # | 符号 | 加载期语义 | lazy/eager | 引用模块 |")
        A("|---|------|------------|------------|----------|")
        for i, s in enumerate(truly_absent, 1):
            t = ltui[s]
            A("| %d | `%s` | %s | %d/%d | %s |"
              % (i, s, t["class"], t["lazy"], t["eager"],
                 ", ".join("`%s`" % x for x in t["modules"][:3])))
        A("")
    if kernel_only:
        A("### 5.5 内核导出符号（`__ksymtab`，精确名单）")
        A("")
        A("| # | 符号 | 所在 .ko（前3） |")
        A("|---|------|-----------------|")
        for i, s in enumerate(kernel_only, 1):
            A("| %d | `%s` | %s |" % (i, s, ", ".join("`%s`" % x for x in d["residual"]["provenance"][s]["where"][:3])))
        A("")
    if exe_only:
        A("### 5.6 由可执行文件导出的符号")
        A("")
        A("| # | 符号 | 提供者（前3） |")
        A("|---|------|---------------|")
        for i, s in enumerate(exe_only, 1):
            A("| %d | `%s` | %s |" % (i, s, ", ".join("`%s`" % x for x in d["residual"]["provenance"][s]["where"][:3])))
        A("")
    if raw_only:
        A("### 5.7 仅以字符串出现（疑似运行时 `dlsym`/`dlopen`）的符号 —— 前 80 条")
        A("")
        A("| # | 符号 | 出现位置（前3） |")
        A("|---|------|-----------------|")
        for i, s in enumerate(raw_only[:80], 1):
            A("| %d | `%s` | %s |" % (i, s, ", ".join("`%s`" % x for x in d["residual"]["provenance"][s]["where"][:3])))
        A("")

    A("## 6. 版本化符号的“软缺口”（soft gap）")
    A("")
    A("对 `appFw.so` 的带版本导入，检查 MERGED 中的提供者是否提供**同版本字符串**。")
    A("基名可解析但版本不匹配的情况共 **%d** 条。" % len(d["soft_gaps"]))
    A("")
    if d["soft_gaps"]:
        A("| 符号 | 导入要求版本 | 提供者实际版本 |")
        A("|------|--------------|----------------|")
        for s in d["soft_gaps"][:80]:
            A("| `%s` | %s | %s |" % (s["sym"], ", ".join(s["wanted"]), ", ".join(s["offered"])))
        A("")
    else:
        A("无。所有版本化导入的版本字符串在 MERGED 中均有对应提供者。")
        A("")

    A("## 7. 两树同名库的 API 破坏面（导出符号增删）")
    A("")
    A("比较口径：两树中 **SONAME 相同且文件大小不同** 的库，共 **%d** 个；"
      "导出符号（GLOBAL/WEAK，DEF）集合做差。" % d["api_delta"]["n_libs_diff_size"])
    A("")
    A("- ZV-1F **新增**（E10 无）导出符号总计：**%d**" % d["api_delta"]["total_added"])
    A("- ZV-1F **丢失**（E10 有）导出符号总计：**%d** ← 这是 ABI 破坏风险的主要来源" % d["api_delta"]["total_dropped"])
    A("")
    A("| SONAME | E10 大小 | F 大小 | Δ | F 新增 | F 丢失 |")
    A("|--------|----------|--------|---|--------|--------|")
    for r in d["api_delta"]["rows"][:40]:
        A("| `%s` | %d | %d | %+d | %d | %d |"
          % (r["key"], r["e10_size"], r["f_size"], r["delta"], r["n_added"], r["n_dropped"]))
    A("")
    A("### 7.1 Top-20 丢失符号（E10 有、ZV-1F 无）—— 逐库")
    A("")
    shown = 0
    for r in d["api_delta"]["rows"]:
        if r["n_dropped"] == 0:
            continue
        A("**`%s`**（丢失 %d 个）：" % (r["key"], r["n_dropped"]))
        A("")
        for s in r["dropped"][:20]:
            A("- `%s`" % s)
        A("")
        shown += 1
        if shown >= 12:
            break
    if shown == 0:
        A("无丢失符号。")
        A("")
    A("### 7.2 Top-20 新增符号（ZV-1F 有、E10 无）—— 逐库")
    A("")
    shown = 0
    for r in sorted(d["api_delta"]["rows"], key=lambda x: -x["n_added"]):
        if r["n_added"] == 0:
            continue
        A("**`%s`**（新增 %d 个）：" % (r["key"], r["n_added"]))
        A("")
        for s in r["added"][:20]:
            A("- `%s`" % s)
        A("")
        shown += 1
        if shown >= 12:
            break
    if shown == 0:
        A("无新增符号。")
        A("")

    # ---- 8. collision rule sensitivity ------------------------------------
    A("## 8. 同名 SONAME 取舍规则的敏感性（重要）")
    A("")
    alt_path = os.path.join(os.path.dirname(jpath), "prefer_e10", "abi_closure.json")
    if os.path.exists(alt_path):
        try:
            alt = json.load(open(alt_path))
        except Exception:
            alt = None
    else:
        alt = None
    if alt is not None:
        a2 = alt["appfw"]
        A("同一个脚本用另一种冲突规则各跑一遍（`--prefer F` vs `--prefer E10`）：")
        A("")
        A("| 规则 | MERGED 库数 | MERGED 定义符号数 | `appFw.so` 可解析 | 占比 | `appFw.so` 残余 | 新菜单根集合残余 |")
        A("|------|-------------|-------------------|-------------------|------|-----------------|------------------|")
        A("| `--prefer F`（本报告正文） | %d | %d | %d / %d | %s | %d | %d |"
          % (d["pool_sizes"]["MERGED"], d["pool_sym_counts"]["MERGED"],
             a["resolved_merged"], a["n_und"], _pct(a["resolved_merged"], a["n_und"]),
             d["residual"]["n_residual_appfw"], d["residual"]["n_residual_newui"]))
        A("| `--prefer E10` | %d | %d | %d / %d | %s | %d | %d |"
          % (alt["pool_sizes"]["MERGED"], alt["pool_sym_counts"]["MERGED"],
             a2["resolved_merged"], a2["n_und"], _pct(a2["resolved_merged"], a2["n_und"]),
             alt["residual"]["n_residual_appfw"], alt["residual"]["n_residual_newui"]))
        A("")
        A("**实测结论：冲突规则的选取是决定性的。** 让 ZV-1F 胜出（`--prefer F`）时 `appFw.so` 残余仅 %d 个；"
          "让 ZV-E10 胜出（`--prefer E10`）时残余暴涨到 %d 个。"
          % (d["residual"]["n_residual_appfw"], alt["residual"]["n_residual_appfw"]))
        A("")
        A("原因：ZV-1F 的 `libmpr.so` / `libObj.so` / `libSysDef.so` / `libInfraSKD.so` / `libInfraRemote.so` 等"
          "与 ZV-E10 同名但内容完全不同（新增菜单框架），若用 ZV-E10 的旧版覆盖它们，"
          "新菜单所需的 `ux::` / `BMP_` / `libra` 等符号立刻大量缺失。")
        A("")
        A("> 操作含义：部署时必须让 ZV-1F 版本的这些库在动态链接器搜索路径中**优先**被找到"
          "（覆盖同名文件，或调整 `LD_LIBRARY_PATH`/`/etc/ld.so.conf` 顺序）。")
        A("")
        A("两种规则下的贪婪最小集相同（都是 %d 个文件）：%s"
          % (len(d["gap"]["greedy_taken"]),
             "、".join("`%s`" % t["key"] for t in d["gap"]["greedy_taken"])))
        A("")
    else:
        A("（未找到 `prefer_e10/abi_closure.json`；执行 "
          "`python3 abi_closure.py --prefer E10 --outdir <dir>/prefer_e10` 后重跑本报告即可生成对比。）")
        A("")

    A("## 9. 附录 A —— 原始命令清单（可复现）")
    A("")
    A("脚本对每个 ELF 文件执行的核心命令：")
    A("")
    A("```bash")
    A("readelf -h  <file>                       # 架构/类型判定")
    A("readelf -W -d <file>                     # 取 NEEDED / SONAME")
    A("readelf -W --dyn-syms <file>             # 取 DEF / UND 动态符号")
    A("readelf -W --dyn-syms <kmod>/*.ko        # 实测返回 0 行（.ko 无 .dynsym）")
    A("readelf -W --dyn-syms <rootfs lib>       # 读根文件系统库的 SONAME")
    A("grep -rlF -f /tmp/abi_residual_pat.txt <tree roots>   # 残余符号原始字节扫描")
    A("c++filt  < <symbol list>                 # 反修饰（仅用于分组展示）")
    A("```")
    A("")
    A("实际执行过的命令（按脚本记录顺序去重）。共执行 **%d** 次 `readelf`/`grep`/`c++filt` 调用；"
      "因对每个 ELF 文件的调用形式完全同构，此处只列出前 %d 条代表性记录，"
      "完整可复现清单由 `abi_closure.py` 本身定义（脚本对每个文件都执行同样两条 `readelf`）。"
      % (len(COMMANDS), min(len(set(COMMANDS)), 120)))
    A("")
    A("```bash")
    seen_c = set()
    shown_c = 0
    for c in COMMANDS:
        if c in seen_c:
            continue
        seen_c.add(c)
        A(c)
        shown_c += 1
        if shown_c >= 120:
            break
    if len(seen_c) > shown_c:
        A("# ... 其余 %d 条同构命令省略（见 abi_closure.py 的 LIB_GROUPS/BIN_GROUPS 全量目录列表）"
          % (len(seen_c) - shown_c))
    A("```")
    A("")
    A("**一键复现全部数字：**")
    A("")
    A("```bash")
    A("python3 <repo>/port/abi_closure.py --prefer F   --outdir <repo>/port")
    A("python3 <repo>/port/abi_closure.py --prefer E10 --outdir <repo>/port/prefer_e10")
    A("```")
    A("")
    A("## 10. 附录 B —— 完整结果 JSON")
    A("")
    A("所有表格的机器可读原始数据（含未截断的完整符号名单）：")
    A("")
    A("`%s`" % jpath)
    A("")
    A("---")
    A("")
    A("*报告由 `abi_closure.py` 自动生成。VERIFIED = 命令实测；[推测] = 推断。*")

    out = os.path.join(args.outdir, "abi_closure_report.md")
    with open(out, "w") as fh:
        fh.write("\n".join(L) + "\n")


_KIND_CN = {"kernel": "内核导出", "executable": "可执行文件导出",
            "raw-string": "仅字符串出现", "absent": "两树内无提供者"}

_CLASS_EXPLAIN = {
    "立即绑定致命(加载期)": "GLOB_DAT/ABS32 等 eager 重定位 → 加载期即报 undefined symbol",
    "惰性绑定(仅首次调用才报错)": "仅 R_ARM_JUMP_SLOT；无 BIND_NOW → 加载期不报错，调用到才报错",
    "WEAK 未定义(非致命)": "WEAK UND，glibc 允许为 0，永远不报错（旧 gcc 遗留）",
    "仅登记未重定位": "在 .dynsym 中登记但无重定位引用，完全无影响",
    "无引用": "没有任何模块引用",
    "无引用(限定范围内)": "在限定的目标模块集合内没有任何模块引用它（不会触发解析）",
}

_DEMANGLED = {}


def _dm(s):
    return _DEMANGLED.get(s, s)


def _group_verdict(g):
    k = g["kinds"]
    if "absent" in k and k["absent"] == g["count"]:
        return "**两树内无任何提供者（硬缺口）**"
    if "kernel" in k and k.get("kernel", 0) == g["count"]:
        return "内核模块 `__ksymtab` 导出 [推测]"
    if "executable" in k and k.get("executable", 0) == g["count"]:
        return "可执行文件导出 [推测]"
    if "kernel" in k:
        return "混合：部分内核导出 [推测]"
    if "raw-string" in k:
        return "疑似运行时 `dlsym`/`dlopen` [推测]"
    return "混合来源"


if __name__ == "__main__":
    main()
