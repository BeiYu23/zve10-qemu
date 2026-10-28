# camtool 在我们这台机器上的运行环境
# 用法: source /root/fw/oss/tools/camtool/env.sh
export PMCA_ROOT=/mnt/build/src/Sony-PMCA-RE          # 本地已有的 Sony-PMCA-RE
export PYTHONPATH=/tmp/camstub                        # libusb_package shim（见下）
# camtool 本体（临时 clone，需要时重 clone）
export CAMTOOL_DIR=/tmp/camtool
# 交叉编译：相机是 armv7l / glibc 2.11.2，必须静态链接
export CAM_CC=/mnt/build/toolchain/armv7-eabihf--glibc--stable-2018.02-1/bin/arm-buildroot-linux-gnueabihf-gcc
export CAM_CC_LD=/mnt/build/toolchain/compat         # 提供 host 侧 libmpfr.so.4
