#!/bin/sh
# Minimal init modelled on chocotan/zve10-qemu source/initramfs/init-ui.sh,
# adapted to our initrd + nflasha layout.
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LD_LIBRARY_PATH=/usr/lib:/lib
unset LD_PRELOAD
exec >/dev/kmsg 2>&1
echo UI_BOOT_ADAPTED
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /rootfs /usr /tmp /setting

# same mounts the vendor init performs, but without loading the 35-module set
mount -t ext2 /dev/dnflasha7  /rootfs
mount -t ext2 /dev/dnflasha15 /usr  || mount -t ext2 -o no-compress /dev/dnflasha15 /usr
[ -d /usr/lib ] || echo UI_NO_USR

load() { echo "MODULE_BEGIN $1"; insmod "$@"; echo "MODULE_END $1 status=$?"; }
load /kmod/devno.ko
load /kmod/dmac.ko
load /usr/kmod/osal_utm.ko
load /usr/kmod/osal_uipc.ko
load /kmod/dma330_stub.ko
load /usr/kmod/backup.ko Backup_load=1 Backup_save=0
load /usr/kmod/utimer.ko
load /usr/kmod/osal_ulogio.ko ulogio_initfile=/setting/ulogio.bin
load /usr/kmod/kikilog.ko
load /usr/kmod/stream.ko
load /usr/kmod/stream2.ko
load /usr/kmod/dmm.ko DmmConfig=/setting/DmmConfig.bin
load /usr/kmod/grm_ma.ko
load /usr/kmod/grm_gles.ko

for spec in 'uipc 251 207' 'backup 251 208' 'dmpgles2 248 29' 'stream 249 7' 'stream2 249 8' 'ldec 251 205'; do
    set -- $spec
    [ -e /dev/"$1" ] || mknod /dev/"$1" c "$2" "$3"
done

echo UI_LAUNCH_BOOTIN
/usr/bin/bootin.elf 'target=qemu ' dmode=3 amode=normal
echo "BOOTIN_EXIT=$?"
exec /bin/sh -i
