#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
反向 ABI 闭包校验
=================

前向检查（appFw.so 的导入能不能被满足）已经做过：MERGED 口径 714/717。

本脚本做**反向**检查 —— 这才是混合固件真正的风险面：

    合并后仍然保留 ZV-E10 版本的那些 ELF（bin/ sbin/ tool/ scenario/ 以及
    硬件绑定的 lib/），它们的导入符号在合并后的库集合里还找不找得到？

原因：ZV-1F 把同一份功能重新拆分了。例：libInfraRemote.so 从 8.7MB 缩到 1.27MB
（导出的 29,474 个符号消失），对应能力搬去了 libInfraSKD.so（10KB → 7.26MB）。
只要"符号在合并集合里某处存在"，运行时就能解析；只有**整棵树都没有**的才算真缺口。

用法：
    python3 check_reverse_closure.py [--tree <repo>/port/out/usr] [--cache DIR]
"""

import argparse
import collections
import json
import os
import re
import subprocess
import sys

UND = 'UND'
SKIP_Ndx = {'UND'}

RE_SYM = re.compile(r'^\s*\d+:\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s*(.*)$')


def parse_dynsyms(path):
    """返回 (defs, undefs) —— 已去掉 @VERSION 后缀"""
    try:
        out = subprocess.run(['readelf', '-W', '--dyn-syms', path],
                             capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        return None, None
    if out.returncode != 0:
        return None, None
    defs, undefs = set(), set()
    for line in out.stdout.splitlines():
        if ':' not in line:
            continue
        parts = line.split()
        # 形如: 126: 00000000 0 NOTYPE WEAK DEFAULT UND __gmon_start__
        if len(parts) < 8 or not parts[0].endswith(':'):
            continue
        try:
            int(parts[0][:-1])
        except ValueError:
            continue
        ndx = parts[6]
        name = parts[7] if len(parts) > 7 else ''
        if not name:
            continue
        name = name.split('@')[0]
        if ndx == UND:
            undefs.add(name)
        else:
            defs.add(name)
    return defs, undefs


def is_elf(path):
    try:
        with open(path, 'rb') as fh:
            return fh.read(4) == b'\x7fELF'
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tree', default='<repo>/port/out/usr')
    ap.add_argument('--manifest', default='<repo>/port/out/decisions.json')
    ap.add_argument('--out', default='<repo>/port/reverse_closure_report.md')
    ap.add_argument('--provider-dir', action='append', default=None,
                    help='额外的库提供者目录（可重复）。默认加入 ZV-E10 的 rootfs，'
                         '因为 libc/libgcc/libpthread/ld 在 nflasha7(/rootfs) 而非 /usr。')
    args = ap.parse_args()

    # 哪些文件来自 ZV-E10（= 需要被保护的存量组件）
    e10_kept = set()
    if os.path.exists(args.manifest):
        for r in json.load(open(args.manifest)):
            if r['action'] in ('KEEP_E10', 'KEEP_E10_ONLY', 'KEEP_E10_REVIEW'):
                e10_kept.add(r['path'])

    print('[*] 收集 ELF …')
    elfs = []
    for dp, dns, fns in os.walk(args.tree):
        for fn in fns:
            p = os.path.join(dp, fn)
            rel = os.path.relpath(p, args.tree)
            if is_elf(p):
                elfs.append((rel, p))
    elfs.sort()
    print(f'    共 {len(elfs)} 个 ELF')

    print('[*] 解析动态符号（这一步最慢）…')
    defs = {}
    undefs = {}
    for i, (rel, p) in enumerate(elfs, 1):
        d, u = parse_dynsyms(p)
        if d is None:
            continue
        defs[rel], undefs[rel] = d, u
        if i % 50 == 0:
            print(f'    {i}/{len(elfs)} …', flush=True)

    # 额外提供者目录（rootfs 的 libc 等不在 /usr 里）
    extra = args.provider_dir or [
        '<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha7_unpacked/lib',
        '<repo>/unpacked_zve10/firmware.tar_unpacked/0700_part_image/dev/nflasha7_unpacked/usr/lib',
    ]
    extra_defs = {}
    for d in extra:
        if not os.path.isdir(d):
            continue
        for dp, dns, fns in os.walk(d):
            for fn in fns:
                p = os.path.join(dp, fn)
                if not is_elf(p):
                    continue
                dd, _ = parse_dynsyms(p)
                if dd:
                    extra_defs[os.path.relpath(p, d) + ' (rootfs)'] = dd
                    print(f'    + 额外提供者 {os.path.relpath(p, d)}: {len(dd)} 个符号')
    defs.update(extra_defs)

    # 提供者 = 合并树里所有库/可执行文件的导出符号并集，
    # 但排除 appFw.so 自身这种"超大单体"以外的？不排除，运行时它确实在全局域里。
    provider = collections.defaultdict(set)
    for rel, d in defs.items():
        for s in d:
            provider[s].add(rel)
    all_defs = set(provider)

    print(f'[=] 合并树内可提供符号 {len(all_defs):,} 个')

    # 只看"保留 ZV-E10 版本"的文件
    roots = [r for r, _ in elfs if r in e10_kept]
    bad = []
    for r in roots:
        missing = sorted(undefs.get(r, set()) - all_defs)
        if missing:
            bad.append((r, missing))

    bad.sort(key=lambda x: -len(x[1]))
    total_missing = sum(len(m) for _, m in bad)

    L = []
    A = L.append
    A('# 反向 ABI 闭包校验报告（合并后，ZV-E10 存量组件还能不能跑）\n')
    A(f'- 合并树：`{args.tree}`')
    A(f'- 合并树内 ELF 总数：**{len(elfs)}**，可提供符号：**{len(all_defs):,}**')
    A(f'- 保留 ZV-E10 版本的 ELF：**{len(roots)}**')
    A(f'- 其中有未解析导入的文件：**{len(bad)}**，涉及符号 **{total_missing}** 个\n')
    A('> 判定口径：只要某个符号在合并树的**任何** ELF 里被导出，就认为运行时可能解析得到'
      '（RTLD_GLOBAL 全局域）。因此这里是**乐观下界**；严格 DT_NEEDED 闭包口径见 '
      '`abi_closure_report.md` §2.1。\n')
    if not bad:
        A('## ✅ 结论：ZV-E10 保留组件全部可解析\n')
        A('没有任何保留文件的导入符号在合并树中缺失。\n')
    else:
        A('## ⚠️ 存在缺口的文件\n')
        A('| 文件 | 缺失符号数 | 缺失符号（最多 12 个）|')
        A('| --- | --- | --- |')
        for r, m in bad[:40]:
            show = ', '.join(f'`{x}`' for x in m[:12]) + (' …' if len(m) > 12 else '')
            A(f'| `{r}` | {len(m)} | {show} |')
        A('')
        # 按符号聚类：哪些符号最普遍缺失
        cnt = collections.Counter()
        for _, m in bad:
            for s in m:
                cnt[s] += 1
        A('## 最普遍缺失的符号（Top 40）\n')
        A('| 符号 | 缺失它的文件数 |')
        A('| --- | --- |')
        for s, c in cnt.most_common(40):
            A(f'| `{s}` | {c} |')
        A('')
        A('## 按前缀聚类\n')
        A('| 前缀 | 符号数 |')
        A('| --- | --- |')
        pre = collections.Counter()
        for s in cnt:
            p = s[:24] if s.startswith('_ZN') else s.split('_')[0]
            pre[p] += 1
        for p, c in pre.most_common(30):
            A(f'| `{p}` | {c} |')
        A('')

    open(args.out, 'w').write('\n'.join(L) + '\n')
    print(f'[+] 报告：{args.out}')
    print(f'[=] 保留 ZV-E10 组件中 {len(bad)} 个文件存在未解析导入，共 {total_missing} 个符号')
    return 0


if __name__ == '__main__':
    sys.exit(main())
