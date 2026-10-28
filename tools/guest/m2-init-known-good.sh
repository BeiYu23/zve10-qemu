#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LD_LIBRARY_PATH=/usr/lib:/lib
unset LD_PRELOAD
exec >/dev/kmsg 2>&1
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
echo INIT_V3_START
load() {
    echo "MODULE_BEGIN $1"
    insmod "$@"
    echo "MODULE_END $1 status=$?"
}
load /kmod/devno.ko
load /kmod/dmac.ko
load /kmod/ldec.ko
load /usr/kmod/osal_utm.ko
load /usr/kmod/osal_uipc.ko
load /usr/kmod/dma330_stub.ko
load /usr/kmod/backup.ko Backup_load=1 Backup_save=0
load /usr/kmod/utimer.ko
load /usr/kmod/osal_ulogio.ko ulogio_initfile=/setting/ulogio.bin
load /usr/kmod/kikilog.ko
load /usr/kmod/stream.ko
load /usr/kmod/stream2.ko
load /usr/kmod/dmm.ko DmmConfig=/setting/DmmConfig.bin
load /usr/kmod/grm_ma.ko
load /usr/kmod/grm_gles.ko
load /kmod/i2c.ko
load /kmod/indctr.ko
load /kmod/udate.ko
load /kmod/mcmn_drv.ko
load /kmod/input.ko key_tbl=0 Input_debug=1
echo INIT_V3_MODULES_DONE
for spec in 'uipc 251 207' 'backup 251 208' 'dmpgles2 248 29' 'stream 249 7' 'stream2 249 8' 'ldec 251 205'; do
    set -- $spec
    [ -e /dev/"$1" ] || mknod /dev/"$1" c "$2" "$3"
done
echo INIT_V3_MKNOD_DONE
( LD_PRELOAD=/usr/lib/libm2probe.so /usr/bin/bootin.elf 'target=qemu ' dmode=3 amode=normal; echo "BOOTIN_EXIT=$?" ) &
echo INIT_V3_LAUNCHED

# WCHANWATCH2: simplest possible, no case/awk
( for it in 1 2 3 4; do
    sleep 20
    echo "===ITER $it==="
    for pp in /proc/[0-9]*; do
      printf "P %s " "${pp#/proc/}"
      cat $pp/comm 2>/dev/null | tr -d "
"
      printf " | "
      cat $pp/wchan 2>/dev/null
      echo ""
    done
  done ) &
while true; do sleep 3600 || true; done
