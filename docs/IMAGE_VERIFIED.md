# 真机 vs 镜像：逐字节对照（2026-10-24）

> 目的：了结"我们的 `nflasha15` dump 是否纯净"这个悬案。
> 方法：用**只读通道**（JIRITSU GET_FILE）从**活相机**读出文件原文，
> 与 `zve10_delivery/nflasha15.img` 里同名文件逐字节比较。
> **全程只读，未向相机写入任何东西。**

## 一、结果：8/8 文件逐字节一致

| 文件 | 真机大小 | md5（真机 = 镜像） | 判定 |
|---|---:|---|---|
| `/usr/bin/im.elf` | 23,240 | `f1c746e3f52559658cd51354b210041e` | ✅ |
| `/usr/bin/bootin.elf` | 15,336 | `d8bfb4f7fc9b71eeb0e9c1ff1ae940bf` | ✅ |
| `/usr/share/app/global.xdb` | 12,252 | `67e80bf841cb81ef38fc401e548aaf75` | ✅ |
| `/usr/share/app/color_cmn.uxc` | 312 | `f468bd3e72ca4e5b948a1c9f1f35c0a1` | ✅ |
| `/usr/lib/libIMDB.so` | 37,204 | `7581e5cd29cc20b732c78fc1501a02d0` | ✅ |
| `/usr/lib/libMWF.so` | 247,776 | `e3787438a193341e6f2c9001fa88b4ff` | ✅ |
| `/usr/lib/libSysDef.so` | 3,267,652 | `0578959c6a1177f926619b485ec098e7` | ✅ |
| **`/usr/lib/libObj.so`** | **21,305,528** | **`12cf982dd3eaded426b0b9c703a681cd`** | ✅ |

⇒ **`nflasha15.img` 是纯净的真机 /usr**。QEMU 侧"是否缺文件/是否被改过"的疑虑可以关闭。

## 二、第三重独立佐证

`color_cmn.uxc` 的 md5 `f468bd3e72ca4e5b948a1c9f1f35c0a1` 正好等于外部仓库
qn06142/zv-e10-re 记录的**原厂 stock 值**（他们做过品红改色实验，stock 与
patch 后的 md5 都公开）。三方（真机 / 我们的镜像 / 第三方记录）一致。

## 三、只读通道的能力与限制（实测）

**能读**：任意**普通文件**，包括 21 MB 的大文件（`libObj.so` 完整传输且
md5 正确）。传输是整文件一次完成。

**读不了**：
- `/proc/*`（含 `/proc/osal/uipc`、`/proc/tmonitor`、`/proc/modules`）
  → 返回 0 字节。原因与块设备相同：相机用 `stat()` 取长度，proc 文件 `st_size=0`。
- `/dev/*`（块设备）同理。

⇒ 所以 **QEMU 要的 `uipc` / `tmonitor` / `maps` 仍然必须走 USB 服务 shell**，
只读文件通道替代不了。

## 四、工具

```sh
PYTHONPATH=/root/fw/zv-e10-toolkit/vendor/Sony-PMCA-RE \
/root/fw/zv-e10-toolkit/.venv/bin/python tools/zve10_dump_files.py \
    --out /tmp/live_dump /usr/lib/libObj.so
```

再与镜像比对：
```sh
debugfs -R "dump /lib/libObj.so /tmp/img_dump/libObj.so" zve10_delivery/nflasha15.img
md5sum /tmp/live_dump/usr_lib_libObj.so /tmp/img_dump/libObj.so
```

## 五、附带结论：`/setting` 镜像确实是旧的

同一轮核对发现 `/setting`（nflasha2）**内容变了**：`ULOGIO.BIN` 内容仍一致，
但 **FAT 簇分配变了**（旧镜像簇 389，真机簇 19，且文件系统是 FAT12 而非 FAT16）。

⇒ **`nflasha15`（/usr）可信；`nflasha2`（/setting）那份 dump 已过期**。
凡涉及 `/setting` 的偏移计算，必须从真机重算（见 `TELNET_ENABLE.md`）。
