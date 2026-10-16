#!/bin/bash
# ZV-E10 QEMU eMMC 镜像构建（安全版）
#
# ★ 教训（Round 21）：在 ext2 镜像上原地 `rm` + `write` 替换文件**不可靠**
#   —— debugfs 的 write 可能失败，一旦把 stderr 吞掉就会**静默丢文件**。
#   本 session 曾因此丢掉 /lib/libMWF.so，导致 App dlopen 失败 → dlerror 文本
#   溢出 0x30 字节堆块 → 堆损坏 → abort，白白追了十几轮。
#
# 因此本脚本改为：**整棵目录树解包到宿主机 → 在宿主机上改 → mke2fs -d 重新打包**，
# 并在最后逐项校验关键文件的存在与大小。
set -e
# ★ 按自己的环境设置（或用环境变量覆盖）
: "${FW:=$(cd "$(dirname "$0")/.." && pwd)}"                 # 本仓库根
: "${TC:=/path/to/arm-buildroot-linux-gnueabihf-}"          # 交叉工具链前缀（含结尾的 -）
: "${SRC:=/path/to/your/nflasha15.img}"                     # 你导出的 /usr 分区
: "${WHOLE:=/path/to/your/ZVE10_EMMC_FULL.img}"             # 你导出的整盘镜像
: "${LOG_FAT:=/path/to/your/nflasha11.img}"                 # /log 分区（可选）
: "${OUT:=./zve10_patched_emmc.img}"
TC=${TC%/}   # 允许带或不带结尾的 -

WORK=$(mktemp -d); trap 'rm -rf $WORK' EXIT

echo "== 1) 解包真机 nflasha15 到宿主机 =="
mkdir -p "$WORK/tree"
debugfs -R "rdump / $WORK/tree" "$SRC" >/dev/null 2>&1
echo "   解包文件数: $(find "$WORK/tree" -type f | wc -l)"

echo "== 2) 在宿主机上打补丁（用 Python 直接改字节，可靠）=="
python3 - "$WORK/tree" "${REF_DIR:-$FW/ref}" <<'PY'
import sys, os, shutil
tree, ref = sys.argv[1], sys.argv[2]
def patch(relsrc, reldst, off, expect, new):
    """把 ref/<relsrc> 的 off 处（须等于 expect）改成 new，写到 tree/<reldst>"""
    src = os.path.join(ref, relsrc)
    dst = os.path.join(tree, reldst)
    d = bytearray(open(src,'rb').read())
    assert d[off:off+len(expect)] == expect, f"{relsrc} 偏移 {off:#x} 不符: {d[off:off+len(expect)].hex()}"
    d[off:off+len(new)] = new
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst,'wb').write(d)
    print(f"   补丁 {reldst} ({len(d):,} B)")

# libMWF.so: MonDrv::PutcharSub -> movs r0,#1; bx lr
patch('usr/lib/libMWF.so', 'lib/libMWF.so', 0x313b8,
      bytes.fromhex('f8b50646'), bytes.fromhex('01207047'))
# MonDrv::Init: FixedAreaSize 从备份读到 0 导致 mmap(len=0) 失败 -> MonQueue 为 NULL
#   311e0: mov r9,r0 -> mov r9,r1  (r1 = open() 的 flags = 4098，非 0)
_p = os.path.join(tree,'lib/libMWF.so'); _d = bytearray(open(_p,'rb').read())
# 注意：不要只把 FixedAreaSize 设成小值(4098)——那样 mmap 出来只有 8KB，
# 而 App 会按 "映射基址 + Physical Addr(0x53400)" 去访问，必然越界。
# 正确做法：直接把 mmap 的长度改成 1MB。
#   31228: mov r0,r8 / mov r1,r9 / stmia.w sp,{r5,r6}
#   ->     mov.w r1,#0x100000        / stmia.w sp,{r5,r6}
assert _d[0x31228:0x31230] == bytes.fromhex('404649468de86000'), _d[0x31228:0x31230].hex()
_d[0x31228:0x31230] = bytes.fromhex('4ff48011') + bytes.fromhex('8de86000')
open(_p,'wb').write(_d); print("   补丁 libMWF.so (mmap 长度 -> 1MB)")
# libOnDemandLoader.so: checkOdlTestOn 恒 0 + Main 立即返回
patch('usr/lib/libOnDemandLoader.so', 'lib/libOnDemandLoader.so', 0x5c2c,
      bytes.fromhex('00f5ca50'), bytes.fromhex('00207047'))
d = bytearray(open(os.path.join(tree,'lib/libOnDemandLoader.so'),'rb').read())
d[0x5784:0x5788] = bytes.fromhex('00207047')
open(os.path.join(tree,'lib/libOnDemandLoader.so'),'wb').write(d)
print("   补丁 lib/libOnDemandLoader.so (Main)")
PY


echo "== 2b) 模块 init 跳过（宿主机上直接改字节）=="
python3 - "$WORK/tree" "${REF_DIR:-$FW/ref}/usr/kmod" "$FW" <<'PY2'
import sys, os, subprocess, re
tree, refkmod, fw = sys.argv[1], sys.argv[2], sys.argv[3]
# KMOD: 模块名 -> 源文件（真机 initrd 里的 /kmod，或 /usr/kmod）
INITRD_KMOD = '${KERNEL_KMOD_DIR:-$FW/kmod}'   # 含 emmc.ko 等
def src_of(name):
    for c in (f"{refkmod}/{name}.ko", f"{INITRD_KMOD}/{name}.ko"):
        if os.path.exists(c): return c
    return None
def patch_init(name, tree_rel):
    src = src_of(name)
    if not src: print(f"   跳过 {name}（无模块）"); return
    TC='<build>/toolchain/armv7-eabihf--glibc--stable-2018.02-1/bin/arm-buildroot-linux-gnueabihf-'
    out=subprocess.run([TC+'readelf','-SW',src],capture_output=True,text=True).stdout
    off=None
    for l in out.splitlines():
        m=re.match(r'\s*\[\s*\d+\]\s+\.init\.text\s+PROGBITS\s+([0-9a-f]+)\s+([0-9a-f]+)',l)
        if m: off=int(m.group(2),16); break
    if off is None: print(f"   跳过 {name}（无 .init.text）"); return
    d=bytearray(open(src,'rb').read())
    d[off:off+8]=bytes.fromhex('0000a0e3')+bytes.fromhex('1eff2fe1')  # mov r0,#0 ; bx lr
    dst=os.path.join(tree, tree_rel, name+'.ko')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst,'wb').write(d)
    print(f"   模块 {name}.ko -> {tree_rel} ({len(d):,} B)")
for n in ('usbg_sen','upm','mag','compass','accel'):
    patch_init(n, 'kmod')
# udate: udate_init_clock 提前返回（/usr/kmod 在真机里也是 /kmod）
src = src_of('udate')
if src:
    d=bytearray(open(src,'rb').read())
    if d[0xbb8:0xbc0]==bytes.fromhex('0dc0a0e1f0d82de9'):
        d[0xbb8:0xbc0]=bytes.fromhex('0000a0e3')+bytes.fromhex('1eff2fe1')
        open(os.path.join(tree,'kmod/udate.ko'),'wb').write(d)
        print("   模块 udate.ko (init_clock 提前返回)")
# backup.ko: backup_suspend+0x828 的 strbeq r4,[r4] -> nop（UIPC 回调里 r4==0 会写地址 0）
src = src_of('backup')
if src:
    d=bytearray(open(src,'rb').read())
    if d[0x950:0x954]==bytes.fromhex('0040c405'):
        d[0x950:0x954]=bytes.fromhex('00f020e3')
        open(os.path.join(tree,'kmod/backup.ko'),'wb').write(d)
        print('   模块 backup.ko (空写->nop)')

# --- jiritsu_init 跳过（"自立"日志子系统，会把堆写坏；真机挂 nflasha6→/tmp/jiritsu）---
_j = os.path.join(tree,'lib/libJiritsu.so')
if os.path.exists(_j):
    _d = bytearray(open(_j,'rb').read())
    # jiritsu_init @0x3be4(.text) -> movs r0,#0; bx lr
    import subprocess as _sp, re as _re
    _o = _sp.run([TC+'readelf','-SW',_j],capture_output=True,text=True).stdout
    for _l in _o.splitlines():
        _m = _re.match(r'\s*\[\s*\d+\]\s+(\.text)\s+PROGBITS\s+([0-9a-f]+)\s+([0-9a-f]+)', _l)
        if _m:
            _fo = int(_m.group(3),16) + (0x3be4 - int(_m.group(2),16))
            _d[_fo:_fo+4] = (0x2000).to_bytes(2,'little') + (0x4770).to_bytes(2,'little')
            open(_j,'wb').write(_d); print("   补丁 libJiritsu.so (jiritsu_init 跳过)")
            break

# --- 安装桩库（App dlopen 的可选功能库；缺失时其错误消息会写穿堆）---
_stubsrc = os.environ.get('STUBS_DIR', '<repo>/stubs/generated')
if os.path.isdir(_stubsrc):
    _n = 0
    for _f in os.listdir(_stubsrc):
        if _f.endswith('.so'):
            import shutil as _sh
            _sh.copy(os.path.join(_stubsrc,_f), os.path.join(tree,'lib',_f)); _n += 1
    print(f"   安装桩库 {_n} 个 (来自 {_stubsrc})")
_stub8 = '/tmp/stub8'
if os.path.isdir(_stub8):
    import shutil as _sh
    _n = 0
    for _f in os.listdir(_stub8):
        if _f.endswith('.so'):
            _sh.copy(os.path.join(_stub8,_f), os.path.join(tree,'lib',_f)); _n += 1
    print(f"   安装补充桩库 {_n} 个")
PY2

echo "== 3) 校验关键文件 =="
for f in lib/libMWF.so lib/libObj.so lib/libIMDB.so lib/libOnDemandLoader.so lib/libInfraAccessLog.so kmod/usbg_sen.ko kmod/upm.ko kmod/udate.ko kmod/backup.ko; do
  p="$WORK/tree/$f"
  if [ -f "$p" ]; then printf "   %-34s %10s B\n" "$f" "$(stat -c%s "$p")"; else echo "   $f ★ 缺失"; exit 1; fi
done

echo "== 4) 重新打包 ext2 =="
dd if=/dev/zero of="$WORK/n15.img" bs=1M count=300 status=none
mkfs.ext2 -q -F -b 1024 -I 128 -O ^resize_inode "$WORK/n15.img"
# 保持与真机一致的 uuid 可选；这里直接用 mke2fs -d 灌入目录
mke2fs -q -F -t ext2 -b 1024 -I 128 -O ^resize_inode -d "$WORK/tree" "$WORK/n15.img" >/dev/null 2>&1 || true
e2fsck -fy "$WORK/n15.img" >/dev/null 2>&1 || true
echo "   n15.img: $(stat -c%s "$WORK/n15.img") B"

echo "== 5) 写入整盘镜像 =="
cp "$WHOLE" "$OUT"
python3 - "$WORK/n15.img" "$OUT" "$FW" <<'PY'
import sys, os
n15, out, fw = sys.argv[1], sys.argv[2], sys.argv[3]
d = open(n15,'rb').read()
with open(out,'r+b') as f:
    f.seek(2*0x200000 + 0x2D800000)
    f.write(d[:0x12C00000].ljust(0x12C00000, b'\0'))
# /log (nflasha11) 造一个合法 FAT32（真机未 dump）
n11 = '/tmp/n11.img'
if os.path.exists(n11):
    with open(out,'r+b') as f:
        f.seek(2*0x200000 + 0x0B800000); f.write(open(n11,'rb').read())
print(f"   写入完成: {out}")
PY
echo "完成: $OUT"
