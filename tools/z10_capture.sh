#!/bin/sh
# ZV-E10 retail-state capture for QEMU bring-up (B2 + B4)
#
# Run this INSIDE the camera's service shell with the camera booted NORMALLY
# and the UI on screen (that is the whole point: we want the state of a
# machine whose menu actually works).
#
#   sh /tmp/z10_capture.sh            # writes /tmp/z10cap/ and a tarball
#   sh /tmp/z10_capture.sh /log       # writes to /log instead (persistent)
#
# Everything here is read-only except for the output files it creates.
# /dev/mem is opened read-only; nothing is written to any device.
#
# Layout read from /setting/KEMCO.TXT (the bootloader's own cmdline):
#   klog.addr=0x035A5000  klog.size=0x20000      (128 KB)
#   alog.addr=0x03524FC0  alog.size=0x60040      (384 KB)
#   tmonitor.addr=0xF00000 tmonitor.size=0x8000  (32 KB)
#   boottime=0x20000@0x03585000
# These are PHYSICAL addresses, which is what dd on /dev/mem wants.

set -u
OUTBASE="${1:-/tmp}"
OUT="$OUTBASE/z10cap"
BB=busybox
mkdir -p "$OUT" 2>/dev/null || { echo "cannot create $OUT"; exit 1; }

say() { echo "[z10cap] $*"; }

# ---------- 0. environment ----------
say "environment"
$BB uname -a            > "$OUT/uname.txt"     2>&1
cat /proc/cmdline       > "$OUT/cmdline.txt"   2>&1
date                    > "$OUT/date.txt"      2>&1
$BB mount               > "$OUT/mounts.txt"    2>&1
$BB df                  > "$OUT/df.txt"        2>&1

# ---------- 1. processes ----------
say "processes"
$BB ps w                > "$OUT/ps.txt"        2>&1
for p in /proc/[0-9]*; do
    pid=${p#/proc/}
    cmd=$(cat "$p/cmdline" 2>/dev/null | tr '\0' ' ')
    echo "$pid $cmd"
done                     > "$OUT/proc_cmdlines.txt" 2>&1

# ---------- 2. the UI process (this is B2) ----------
PID=""
for cand in `$BB pidof im.elf 2>/dev/null`; do PID="$cand"; break; done
if [ -z "$PID" ]; then
    # fall back: whichever process has viewUnified mapped
    for p in /proc/[0-9]*; do
        if grep -qs viewUnified "$p/maps" 2>/dev/null; then PID=${p#/proc/}; break; fi
    done
fi

if [ -n "$PID" ]; then
    say "UI process pid=$PID"
    echo "$PID"                 > "$OUT/ui_pid.txt"
    cat /proc/$PID/maps         > "$OUT/ui_maps.txt"     2>&1   # ★ B2
    cat /proc/$PID/status       > "$OUT/ui_status.txt"   2>&1
    cat /proc/$PID/stat         > "$OUT/ui_stat.txt"     2>&1
    ls -l /proc/$PID/fd         > "$OUT/ui_fd.txt"       2>&1
    ls -l /proc/$PID/task       > "$OUT/ui_tasks.txt"    2>&1
    for t in /proc/$PID/task/*; do
        tid=${t##*/}
        echo "$tid $(cat $t/comm 2>/dev/null) $(cat $t/wchan 2>/dev/null)"
    done                        > "$OUT/ui_threads.txt"  2>&1
    # which libraries are actually resident, and in what order
    grep -o '/[^ ]*\.so[^ ]*' "$OUT/ui_maps.txt" | sort -u > "$OUT/ui_libs.txt" 2>&1
    # distinguish the menu resources that ARE mapped from those that are not
    grep -E '\.uxc|\.xdb|\.uxb' "$OUT/ui_maps.txt" > "$OUT/ui_res_mapped.txt" 2>&1
else
    say "WARNING: no process with viewUnified mapped - is the UI up?"
fi

# ---------- 3. memory rings (this is B4) ----------
say "memory rings"
if [ -r /dev/mem ]; then
    # bs=1 keeps the offsets exact (0x03524FC0 is not 1 KiB aligned).
    # /dev/mem ignores the page offset waste because dd seeks once.
    $BB dd if=/dev/mem of="$OUT/alog.bin"     bs=1 skip=$((0x03524FC0)) count=$((0x60040)) 2>/dev/null
    $BB dd if=/dev/mem of="$OUT/klog.bin"     bs=1 skip=$((0x035A5000)) count=$((0x20000)) 2>/dev/null
    $BB dd if=/dev/mem of="$OUT/tmonitor.bin" bs=1 skip=$((0x00F00000)) count=$((0x8000))  2>/dev/null
    $BB dd if=/dev/mem of="$OUT/boottime.bin" bs=1 skip=$((0x03585000)) count=$((0x20000)) 2>/dev/null
    for f in alog klog tmonitor boottime; do
        if [ -s "$OUT/$f.bin" ]; then
            say "  $f.bin $(wc -c < "$OUT/$f.bin") bytes, non-zero: $(tr -d '\000' < "$OUT/$f.bin" | wc -c)"
        fi
    done
else
    say "  /dev/mem not readable; falling back to /proc interfaces"
fi
# whatever /proc exposes anyway
cat /proc/kmsg 2>/dev/null | head -c 65536 > "$OUT/proc_kmsg.txt" 2>&1
$BB dmesg      2>/dev/null              > "$OUT/dmesg.txt"       2>&1
cat /proc/tmonitor 2>/dev/null | head -c 65536 > "$OUT/proc_tmonitor.txt" 2>&1

# ---------- 4. driver / platform state ----------
say "platform state"
ls -l /proc/udm 2>/dev/null             > "$OUT/proc_udm_ls.txt"  2>&1
for d in /proc/udm/*; do
    [ -r "$d" ] && echo "=== $d ===" && cat "$d" 2>/dev/null
done                                     > "$OUT/proc_udm_all.txt" 2>&1
cat /proc/modules                        > "$OUT/proc_modules.txt" 2>&1
cat /proc/interrupts                     > "$OUT/proc_interrupts.txt" 2>&1
cat /proc/iomem                          > "$OUT/proc_iomem.txt"   2>&1
cat /proc/meminfo                        > "$OUT/proc_meminfo.txt" 2>&1
ls -l /dev                               > "$OUT/dev_ls.txt"       2>&1
ls -l /usr/lib                           > "$OUT/usr_lib_ls.txt"   2>&1
ls -l /usr/share/app                    > "$OUT/usr_share_app_ls.txt" 2>&1

# ---------- 5. settings that select boot behaviour ----------
say "settings"
ls -lR /setting 2>/dev/null             > "$OUT/setting_ls.txt"    2>&1
[ -f /setting/mode/dmode ]   && cat /setting/mode/dmode   > "$OUT/setting_dmode.txt" 2>&1
[ -f /setting/mode/preload ] && cat /setting/mode/preload > "$OUT/setting_preload.txt" 2>&1
[ -f /setting/env.txt ]      && cat /setting/env.txt      > "$OUT/setting_env.txt" 2>&1
[ -f /etc/ld.so.preload ]    && cat /etc/ld.so.preload    > "$OUT/etc_ldso_preload.txt" 2>&1

# ---------- 6. package ----------
say "packaging"
cd "$OUTBASE" && $BB tar czf "$OUTBASE/z10cap.tgz" z10cap 2>/dev/null
if [ -s "$OUTBASE/z10cap.tgz" ]; then
    say "OK: $OUTBASE/z10cap.tgz ($(wc -c < "$OUTBASE/z10cap.tgz") bytes)"
    say "pull it out with the same channel you used to get in"
else
    say "tar failed; the loose files are in $OUT"
fi
