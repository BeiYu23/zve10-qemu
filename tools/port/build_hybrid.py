#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ZV-1F 新菜单 → ZV-E10 混合固件装配器  (v2: 保真版)
==================================================

v1 从 fwtool 解出来的文件树做合并 —— 那棵树是**有损的**：
  * 丢了全部符号链接（ZV-E10 的 /usr 里有 65 个，包括 `libstdc++.so.6`）
  * 丢了权限位（生成出来的镜像里所有文件都是 0644，可执行文件全不可执行）
  * 丢了属主（原厂是 57285:1000）

v2 改为从**原厂 ext2 分区镜像**用 `debugfs rdump` 取树（<repo>/ref_e10/usr 等），
并用 `mke2fs -d` 直接按目录树生成 ext2 镜像 —— 权限、符号链接、属主、时间戳全部保留。

用法：
    # 1) 先取保真树（只需一次）
    mkdir -p ref_e10/usr && debugfs -R "rdump / <repo>/ref_e10/usr" <nflasha15 镜像>
    # 2) 装配
    python3 port/build_hybrid.py --hash --materialize --image
"""

import argparse
import csv
import hashlib
import json
import os
import shutil
import subprocess
import sys

# ---------------------------------------------------------------- 常量

PART_USR_ZVE10 = 314_572_800          # /dev/nflasha15 300 MiB (partinf.conf)
PART_USR_ZV1F = 524_288_000           # ZV-1F 的同一分区是 500 MiB
ORIG_UUID = 'c509f8ed-a432-4966-beb5-25ba065631ea'   # 原厂 ZV-E10 nflasha15 UUID

DEFAULT_E10 = '<repo>/ref_e10/usr'
DEFAULT_ZV1F = '<repo>/ref_zv1f/usr'

# 硬件绑定：ZV-1F 版本依赖不同硬件，必须保留 ZV-E10 的
KEEP_E10_KMOD = {
    'compass.ko', 'usb_darwin.ko', 'usb_max14648.ko', 'ms_drv.ko', 'mcmn_drv.ko',
    'codec_drv.ko', 'upm.ko', 'bcmdhd.ko', 'mmc_drv.ko',
}

# 整目录硬件绑定：sabin = ISP/DSP 图像管线固件；codec = 视频编解码固件
NEVER_TAKE_F_PREFIX = ('sabin/', 'codec/', 'data/')

# 新 UI 的运行时资源目录
ALWAYS_TAKE_F_PREFIX = ('share/app/',)

# 平台框架层：同名不同即取 ZV-1F 版本
FRAMEWORK_TAKE_F_PREFIX = ('lib/', 'scenario/')

# 新 UI 本体
ALWAYS_TAKE_F_EXACT = {'lib/appFw.so', 'lib/appFwBoot.so'}

# 老菜单专用库：ZV-1F 不带，且依赖已被移除的旧 MPR API（见 reverse_closure_report.md）
DROP_LEGACY_EXACT = {
    'lib/libInfraHighlight.so',
    'lib/libInfraBGMMeta.so',
    'lib/libInfraCacheMgr.so',
    'scenario/MPR_SCN_FLASH_BACKUP_INIT.so',
    'scenario/MPR_SCN_INSTALL_MAP_DEMOMOVIE.so',
}

# 老菜单 UI：ZV-E10 独有、新 UI 不需要，且占 ~97 MiB
DROP_OLD_UI_PREFIX = ('lib/viewUnified', 'lib/libBizFw', 'share/app/view')
DROP_OLD_UI_EXACT = {
    'lib/CautionConfig.so', 'lib/CautionRsrcConfig.so', 'lib/ModelCautionConfig.so',
    'lib/libInfraMakeScenario.so', 'lib/libmprctrl.so', 'lib/libmprdummy.so',
    'share/app/IRShooting.uxc',
    'share/app/image_DSC_Entry_Only_43.uxc', 'share/app/image_DSC_Only_43.uxc',
    'share/app/image_DSLR_Only_43.uxc', 'share/app/image_EVF.uxc',
    'share/app/image_Fn_Menu_43.uxc', 'share/app/image_Rich_Help_Guide_43.uxc',
    'share/app/image_cmn_43.uxc', 'share/app/image_cmn_o_direct_43.uxc',
}

# 可选载荷：ZV-1F 屏幕朗读语音库（107.9 MiB，ZV-E10 无此功能）
OPTIONAL_F_PREFIX = ('share/screenreader/',)


# ---------------------------------------------------------------- 工具

def sha256(path, bufsize=1 << 20):
    h = hashlib.sha256()
    with open(path, 'rb') as fh:
        while True:
            b = fh.read(bufsize)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def inventory(root):
    """保真清单：{relpath: {'size': int, 'link': str|None, 'mode': int}}

    * 符号链接单独记录目标（不跟随）
    * 排除 fwtool 解包器留下的 *_unpacked* 中间目录
    """
    out = {}
    skipped = []
    for dirpath, dirnames, filenames in os.walk(root, followlinks=False):
        rel_dir = os.path.relpath(dirpath, root)
        # 目录符号链接：记录，但不递归
        keep = []
        for d in list(dirnames):
            p = os.path.join(dirpath, d)
            rel = os.path.normpath(os.path.join(rel_dir, d)) if rel_dir != '.' else d
            if os.path.islink(p):
                out[rel] = {'size': os.lstat(p).st_size,
                            'link': os.readlink(p), 'mode': 0o120777}
                dirnames.remove(d)
            elif d.endswith('_unpacked') or '_unpacked.' in d:
                dirnames.remove(d)
                skipped.append(rel + '/ (解包中间目录)')
            else:
                keep.append(d)
        for fn in filenames:
            rel = os.path.normpath(os.path.join(rel_dir, fn)) if rel_dir != '.' else fn
            if '_unpacked' in rel:
                skipped.append(rel)
                continue
            p = os.path.join(dirpath, fn)
            try:
                st = os.lstat(p)
            except OSError as e:
                skipped.append(f'{rel} ({e})')
                continue
            if os.path.islink(p):
                out[rel] = {'size': st.st_size, 'link': os.readlink(p),
                            'mode': st.st_mode}
            elif os.path.isfile(p):
                out[rel] = {'size': st.st_size, 'link': None, 'mode': st.st_mode}
            else:
                skipped.append(f'{rel} (非普通文件)')
    return out, skipped


def size_h(n):
    return f'{n:,} B ({n / 2**20:.2f} MiB)'


def same(a, b):
    return a['size'] == b['size'] and a['link'] == b['link']


# ---------------------------------------------------------------- 决策

def decide(e10, f, with_screenreader=False, conservative=False,
           aggressive=False, lib_take_f=None):
    lib_take_f = lib_take_f or set()
    rows = []
    for path in sorted(set(e10) | set(f)):
        in_e, in_f = path in e10, path in f
        es, fs = e10.get(path), f.get(path)
        e_size = es['size'] if es else None
        f_size = fs['size'] if fs else None
        is_link = bool((es or fs).get('link'))
        top = path.split('/')[0]
        base = os.path.basename(path)
        hw_bound = path.startswith(NEVER_TAKE_F_PREFIX)

        if in_e and in_f:
            if same(es, fs):
                rows.append(dict(path=path, action='KEEP_E10', source='e10',
                                 e10_size=e_size, f_size=f_size,
                                 reason='同名同大小（符号链接同目标）'))
                continue
            if hw_bound and not aggressive:
                rows.append(dict(path=path, action='KEEP_E10', source='e10',
                                 e10_size=e_size, f_size=f_size,
                                 reason=f'硬件绑定子系统 {top}/，保留 ZV-E10 版'))
            elif path.startswith('kmod/') and base in KEEP_E10_KMOD and not aggressive:
                rows.append(dict(path=path, action='KEEP_E10', source='e10',
                                 e10_size=e_size, f_size=f_size,
                                 reason=f'硬件绑定驱动 {base}，保留 ZV-E10 版'))
            elif (path in ALWAYS_TAKE_F_EXACT or path.startswith(ALWAYS_TAKE_F_PREFIX)
                  or path.startswith(FRAMEWORK_TAKE_F_PREFIX)
                  or path in lib_take_f or aggressive):
                rows.append(dict(path=path, action='TAKE_F', source='zv1f',
                                 e10_size=e_size, f_size=f_size,
                                 reason='新 UI / 新中间件层 → 取 ZV-1F 版本'))
            else:
                rows.append(dict(path=path, action='KEEP_E10_REVIEW', source='e10',
                                 e10_size=e_size, f_size=f_size,
                                 reason=f'{top}/ 下存在差异但未被新 UI 依赖链引用，'
                                        '默认保留 ZV-E10（可用 --aggressive 换成 ZV-1F）'))
        elif in_f:
            if any(path.startswith(p) for p in OPTIONAL_F_PREFIX) and not with_screenreader:
                rows.append(dict(path=path, action='DROP_F_OPTIONAL', source='-',
                                 e10_size=None, f_size=f_size,
                                 reason='可选载荷（屏幕朗读语音库），ZV-E10 无此功能，'
                                        '删除以适配 300MiB 分区；需要时 --with-screenreader'))
            elif (hw_bound or (path.startswith('kmod/') and base in KEEP_E10_KMOD)) \
                    and not aggressive:
                rows.append(dict(path=path, action='DROP_F_HARDWARE', source='-',
                                 e10_size=None, f_size=f_size,
                                 reason=f'ZV-1F 独有但属硬件绑定（{top}/{base}），不引入'))
            else:
                rows.append(dict(path=path, action='ADD_F', source='zv1f',
                                 e10_size=None, f_size=f_size,
                                 reason='ZV-1F 独有（新 UI / 新中间件 / 新资源）'))
        else:
            is_old_ui = path in DROP_OLD_UI_EXACT or any(
                path.startswith(p) for p in DROP_OLD_UI_PREFIX)
            if path in DROP_LEGACY_EXACT and not conservative:
                rows.append(dict(path=path, action='DROP_LEGACY', source='-',
                                 e10_size=e_size, f_size=None,
                                 reason='老菜单专用且依赖已被移除的旧 MPR API；'
                                        'ZV-1F 本身不带此文件 ⇒ 删除（反向 ABI 校验实测）'))
            elif is_old_ui and not conservative:
                rows.append(dict(path=path, action='DROP_OLD_UI', source='-',
                                 e10_size=e_size, f_size=None,
                                 reason='ZV-E10 老菜单资源，新 UI 不再引用'))
            else:
                rows.append(dict(path=path, action='KEEP_E10_ONLY', source='e10',
                                 e10_size=e_size, f_size=None,
                                 reason='ZV-E10 独有；非老菜单资源，默认保留' +
                                        ('（conservative 模式）' if conservative else '')))
    return rows


def totals(rows):
    t = {}
    for r in rows:
        t.setdefault(r['action'], [0, 0])
        t[r['action']][0] += 1
        if r['action'] in ('ADD_F', 'TAKE_F'):
            t[r['action']][1] += r['f_size'] or 0
        elif r['action'] in ('KEEP_E10', 'KEEP_E10_ONLY', 'KEEP_E10_REVIEW'):
            t[r['action']][1] += r['e10_size'] or 0
        elif r['action'] in ('DROP_OLD_UI', 'DROP_LEGACY'):
            t[r['action']][1] += r['e10_size'] or 0
        else:
            t[r['action']][1] += r['f_size'] or 0
    return t


def merged_bytes(rows):
    n = 0
    for r in rows:
        if r['action'] in ('ADD_F', 'TAKE_F'):
            n += r['f_size'] or 0
        elif r['action'] in ('KEEP_E10', 'KEEP_E10_ONLY', 'KEEP_E10_REVIEW'):
            n += r['e10_size'] or 0
    return n


# ---------------------------------------------------------------- 物化

def materialize(rows, e10_root, f_root, out_root):
    """按决策搭出保真合并树：普通文件用硬链接（同分区零拷贝、保留权限属主），
    符号链接按原样重建。返回 (文件数, 符号链接数, 逻辑字节数)。"""
    if os.path.exists(out_root):
        shutil.rmtree(out_root)
    os.makedirs(out_root)
    n = ln = tot = 0
    links = []
    for r in rows:
        if r['action'] in ('DROP_OLD_UI', 'DROP_LEGACY', 'DROP_F_OPTIONAL',
                           'DROP_F_HARDWARE'):
            continue
        src_root = f_root if r['action'] in ('ADD_F', 'TAKE_F') else e10_root
        src = os.path.join(src_root, r['path'])
        dst = os.path.join(out_root, r['path'])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.islink(src):
            os.symlink(os.readlink(src), dst)
            links.append((r['path'], os.readlink(src)))
            ln += 1
        else:
            try:
                os.link(src, dst)
            except OSError:
                # 跨设备时的回退。copy2 只保留权限与时间戳，不含属主，
                # 必须显式 chown，否则镜像里会变成 0:0（原厂是 57285:1000）。
                shutil.copy2(src, dst)
                st = os.lstat(src)
                try:
                    os.chown(dst, st.st_uid, st.st_gid)
                except OSError:
                    pass
            n += 1
        tot += os.path.getsize(dst) if not os.path.islink(dst) else 0

    # 空目录也要保留：inventory 只跟踪文件与符号链接，/usr/local/lib 这类
    # 纯空目录会整条丢失，这里按两侧目录集合补建。
    made = 0
    for root in (e10_root, f_root):
        for dirpath, dirnames, filenames in os.walk(root):
            rel = os.path.relpath(dirpath, root)
            if rel == '.':
                rel = ''
            dst = os.path.join(out_root, rel) if rel else out_root
            if os.path.isdir(dst):
                continue
            os.makedirs(dst, exist_ok=True)
            st = os.stat(dirpath)
            try:
                os.chmod(dst, st.st_mode & 0o7777)
                os.chown(dst, st.st_uid, st.st_gid)
            except OSError:
                pass
            made += 1

    with open(os.path.join(os.path.dirname(out_root), 'symlinks.json'), 'w') as fh:
        json.dump(links, fh, indent=1)
    if made:
        print(f'    补建空目录 {made} 个')
    return n, ln, tot


def make_image(tree_root, out_img, size_bytes=PART_USR_ZVE10, uuid=ORIG_UUID):
    """用 mke2fs -d 直接按目录树生成 ext2 镜像（权限/属主/符号链接全部保留）。
    几何参数对齐原厂：block=1024, bytes-per-inode=4096, reserved=0, label=usr。"""
    if os.path.exists(out_img):
        os.remove(out_img)
    with open(out_img, 'wb') as fh:
        fh.truncate(size_bytes)
    cmd = ['mke2fs', '-t', 'ext2', '-F', '-q', '-b', '1024', '-i', '4096',
           '-O', '^has_journal,^extent,^64bit', '-m', '0', '-L', 'usr',
           '-d', tree_root, '-U', uuid, out_img]
    subprocess.run(cmd, check=True)
    return out_img


# ---------------------------------------------------------------- 报告

def write_reports(rows, t, out_dir, e10_total, f_total, skipped, args):
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, 'manifest.csv'), 'w', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=['path', 'action', 'source',
                                           'e10_size', 'f_size', 'reason'])
        w.writeheader()
        for r in rows:
            w.writerow(r)

    mb = merged_bytes(rows)
    limit = PART_USR_ZVE10
    est = int(mb * 1.135)
    ok = est <= limit

    L = []
    A = L.append
    A('# 混合固件装配报告（ZV-1F 新菜单 → ZV-E10）\n')
    A(f'生成器：`port/build_hybrid.py` v2（保真）　参数：screenreader={args.with_screenreader} '
      f'conservative={args.conservative}\n')
    A('## 一、体积核算\n')
    A('| 项 | 值 |')
    A('| --- | --- |')
    A(f'| ZV-E10 `/usr` 原始内容 | {size_h(e10_total)} |')
    A(f'| ZV-1F `/usr` 原始内容 | {size_h(f_total)} |')
    A(f'| **合并后内容** | **{size_h(mb)}** |')
    A(f'| `/dev/nflasha15` 分区上限（ZV-E10） | {size_h(limit)} |')
    A(f'| ext2 镜像估算（×1.135） | {size_h(est)} |')
    A(f'| 判定 | {"✅ **装得下**" if ok else "❌ **超出，需要继续裁剪**"}'
      f'（余量 {size_h(limit - est)}）|\n')
    A('> ZV-1F 的同一分区是 500 MiB —— 直接写它的镜像是不可能的。\n')

    A('## 二、决策统计\n')
    A('| 动作 | 文件数 | 涉及字节 | 说明 |')
    A('| --- | --- | --- | --- |')
    desc = {
        'KEEP_E10': '同名同大小或硬件绑定，保留 ZV-E10 文件',
        'KEEP_E10_ONLY': 'ZV-E10 独有且非老菜单资源，保留',
        'KEEP_E10_REVIEW': '存在差异但未被新 UI 依赖链引用，默认保留 ZV-E10（待复核）',
        'TAKE_F': '新 UI / 新中间件层 → 取 ZV-1F 版本',
        'ADD_F': 'ZV-1F 独有 → 新增',
        'DROP_OLD_UI': 'ZV-E10 老菜单资源 → 删除',
        'DROP_LEGACY': '旧 MPR API 消费者，ZV-1F 不带 → 删除',
        'DROP_F_OPTIONAL': 'ZV-1F 可选载荷 → 删除（体积）',
        'DROP_F_HARDWARE': 'ZV-1F 独有但硬件绑定 → 不引入',
    }
    for k in ('TAKE_F', 'ADD_F', 'KEEP_E10', 'KEEP_E10_REVIEW', 'KEEP_E10_ONLY',
              'DROP_OLD_UI', 'DROP_LEGACY', 'DROP_F_OPTIONAL', 'DROP_F_HARDWARE'):
        if k in t:
            A(f'| `{k}` | {t[k][0]} | {t[k][1]:,} | {desc.get(k, "")} |')
    A('')

    A('## 三、ZV-1F 必须搬过来的文件（TAKE_F，前 25）\n')
    A('| 文件 | ZV-E10 | ZV-1F |')
    A('| --- | --- | --- |')
    for r in sorted((x for x in rows if x['action'] == 'TAKE_F'),
                    key=lambda x: -(x['f_size'] or 0))[:25]:
        A(f'| `{r["path"]}` | {r["e10_size"]:,} | {r["f_size"]:,} |')
    A('')

    A('## 四、删除的老菜单文件（DROP_OLD_UI，前 20）\n')
    A('| 文件 | 体积 |')
    A('| --- | --- |')
    for r in sorted((x for x in rows if x['action'] == 'DROP_OLD_UI'),
                    key=lambda x: -(x['e10_size'] or 0))[:20]:
        A(f'| `{r["path"]}` | {r["e10_size"]:,} |')
    A('')

    A('## 五、权限与符号链接（v2 保真关键）\n')
    A('原厂 `/usr` 里有 65 个符号链接、594 个可执行文件。v1 从 fwtool 解出的树'
      '**丢失**了这些元数据，生成出的镜像里所有文件都是 0644。')
    A('v2 改为 `debugfs rdump` 取树 + `mke2fs -d` 生成镜像，权限/属主/链接全部保留。\n')

    if skipped:
        A('## 六、被排除的非普通文件/中间产物\n')
        A(f'共 {len(skipped)} 项，例如：')
        A('```')
        for s in skipped[:10]:
            A(s)
        A('```\n')
    A('')
    A('## 七、回滚\n')
    A('```sh')
    A('dd if=/dev/nflasha15 of=/mnt/sd/zve10_usr_orig.bin bs=1M')
    A('md5sum /mnt/sd/zve10_usr_orig.bin   # 存档；回滚就是写回去')
    A('```')

    with open(os.path.join(out_dir, 'summary.md'), 'w') as fh:
        fh.write('\n'.join(L) + '\n')
    with open(os.path.join(out_dir, 'decisions.json'), 'w') as fh:
        json.dump(rows, fh, indent=1, ensure_ascii=False)
    return mb, est, ok


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--e10', default=DEFAULT_E10)
    ap.add_argument('--zv1f', default=DEFAULT_ZV1F)
    ap.add_argument('--out', default='<repo>/port/out')
    ap.add_argument('--with-screenreader', action='store_true')
    ap.add_argument('--conservative', action='store_true')
    ap.add_argument('--aggressive', action='store_true')
    ap.add_argument('--lib-take-f', default=None)
    ap.add_argument('--materialize', action='store_true')
    ap.add_argument('--image', action='store_true')
    ap.add_argument('--hash', action='store_true')
    args = ap.parse_args()

    if not os.path.isdir(args.e10):
        print(f'[!] 保真树不存在：{args.e10}\n'
              f'    先执行：mkdir -p {os.path.dirname(args.e10)} && '
              f'debugfs -R "rdump / {args.e10}" <nflasha15 镜像>')
        return 2

    print('[*] 扫描保真文件树 …')
    e10, se = inventory(args.e10)
    f, sf = inventory(args.zv1f)
    print(f'    ZV-E10: {len(e10)} 项, {size_h(sum(v["size"] for v in e10.values()))}')
    print(f'    ZV-1F : {len(f)} 项, {size_h(sum(v["size"] for v in f.values()))}')
    print(f'    符号链接: ZV-E10 {sum(1 for v in e10.values() if v["link"])} 个, '
          f'ZV-1F {sum(1 for v in f.values() if v["link"])} 个')

    lib_take_f = set()
    if args.lib_take_f and os.path.exists(args.lib_take_f):
        # 条目既可以写相对 /usr 的完整路径（如 share/avsystemsound/beeplist.txt），
        # 也可以只写库名（如 libObj.so，会被补成 lib/libObj.so）。
        lib_take_f = {x if '/' in x else f'lib/{x}'
                      for x in json.load(open(args.lib_take_f))}
        print(f'    载入强制 TAKE_F 清单：{len(lib_take_f)} 项')

    rows = decide(e10, f, with_screenreader=args.with_screenreader,
                  conservative=args.conservative, aggressive=args.aggressive,
                  lib_take_f=lib_take_f)

    if args.hash:
        print('[*] sha256 复核（只允许框架层的同名同大小文件升级为 TAKE_F）…')
        upgradable = ('lib/', 'scenario/', 'share/app/')
        fixed = 0
        for r in rows:
            if not (r['path'].startswith(upgradable) or r['path'] in ALWAYS_TAKE_F_EXACT):
                continue
            if (r['action'] == 'KEEP_E10' and r['e10_size'] == r['f_size'] and r['f_size']
                    and not os.path.islink(os.path.join(args.e10, r['path']))):
                a = sha256(os.path.join(args.e10, r['path']))
                b = sha256(os.path.join(args.zv1f, r['path']))
                if a != b:
                    r['action'] = 'TAKE_F'
                    r['reason'] = '同名同大小但内容不同（sha256 复核）→ 取新菜单版本'
                    fixed += 1
        print(f'    修正 {fixed} 项')

    t = totals(rows)
    mb, est, ok = write_reports(rows, t, args.out,
                                sum(v['size'] for v in e10.values()),
                                sum(v['size'] for v in f.values()), se + sf, args)

    print(f'\n[=] 合并后内容 {size_h(mb)}　ext2 估算 {size_h(est)}　'
          f'分区上限 {size_h(PART_USR_ZVE10)}　{"✅ 装得下" if ok else "❌ 超限"}')
    print(f'    决策表: {args.out}/manifest.csv')
    print(f'    报告  : {args.out}/summary.md')

    if args.materialize:
        tree = os.path.join(args.out, 'usr')
        n, ln, tot = materialize(rows, args.e10, args.zv1f, tree)
        print(f'[+] 合并树: {tree}  ({n} 个文件 + {ln} 个符号链接, {size_h(tot)})')
    if args.image:
        tree = os.path.join(args.out, 'usr')
        if not os.path.isdir(tree):
            print('[!] --image 需要先 --materialize')
            return 2
        img = os.path.join(args.out, 'nflasha15_hybrid.ext2')
        make_image(tree, img, PART_USR_ZVE10, ORIG_UUID)
        print(f'[+] 镜像: {img} ({size_h(os.path.getsize(img))})')

    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
