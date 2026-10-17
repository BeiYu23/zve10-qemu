#!/bin/bash
# 在我自己的镜像上做 A/B 注入 → 产出 <build>/verify_ab.img（不动任何他人文件 ✓）
set -e
IMG=<build>/verify_patched.img      # 我自己的镜像（含补丁版 libObj.so ✓）
OUT=<build>/verify_ab.img
N15_OFF=$((2*0x200000 + 0x2D800000)); N15_SZ=$((300*1024*1024))
W=$(mktemp -d); trap 'rm -rf $W' EXIT
echo "== 1) 抽出 /usr 分区 =="
cp "$IMG" "$OUT"
dd if="$OUT" of="$W/n15.img" bs=1M skip=$((N15_OFF/1048576)) count=300 status=none
echo "== 2) 解包 =="
mkdir -p "$W/tree"; debugfs -R "rdump / $W/tree" "$W/n15.img" >/dev/null 2>&1
echo "   文件数: $(find "$W/tree" -type f | wc -l)"
echo "== 3) 注入 =="
cp /tmp/libObj_live.so "$W/tree/lib/libObj_orig.so"; cp <repo>/libObj_patched.so "$W/tree/lib/libObj.so"; cp <repo>/libhook.so "$W/tree/lib/libhook.so"
for b in bin/net_cmdd.elf bin/bootin.elf bin/bsa_server; do
  [ -f "$W/tree/$b" ] || continue
  cp "$W/tree/$b" "$W/tree/$b.real"
  cp <repo>/abtest/ab_wrapper.sh "$W/tree/$b"; chmod 755 "$W/tree/$b"
  echo "   包装 $b  (real=$(stat -c%s "$W/tree/$b.real") B)"
done
for f in lib/libObj.so lib/libObj_orig.so lib/libhook.so; do printf "   %-40s %10s B\n" "$f" "$(stat -c%s "$W/tree/$f")"; done
echo "== 4) 重打包 =="
rm -f "$W/n15new.img"; dd if=/dev/zero of="$W/n15new.img" bs=1M count=300 status=none
mke2fs -q -F -t ext2 -b 1024 -I 128 -O ^resize_inode -d "$W/tree" "$W/n15new.img" >/dev/null 2>&1 || true
e2fsck -fy "$W/n15new.img" >/dev/null 2>&1 || true
echo "== 5) 写回我的镜像 =="
python3 - "$W/n15new.img" "$OUT" "$N15_OFF" <<'PY'
import sys
src,out,off=sys.argv[1],sys.argv[2],int(sys.argv[3])
d=open(src,'rb').read()
with open(out,'r+b') as f:
    f.seek(off); f.write(d[:0x12C00000].ljust(0x12C00000,b'\0'))
print("   写入完成:",out)
PY
echo "完成: $OUT"
