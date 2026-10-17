# 反向 ABI 闭包校验报告（合并后，ZV-E10 存量组件还能不能跑）

- 合并树：`<repo>/port/out/usr`
- 合并树内 ELF 总数：**392**，可提供符号：**73,817**
- 保留 ZV-E10 版本的 ELF：**296**
- 其中有未解析导入的文件：**192**，涉及符号 **384** 个

> 判定口径：只要某个符号在合并树的**任何** ELF 里被导出，就认为运行时可能解析得到（RTLD_GLOBAL 全局域）。因此这里是**乐观下界**；严格 DT_NEEDED 闭包口径见 `abi_closure_report.md` §2.1。

## ⚠️ 存在缺口的文件

| 文件 | 缺失符号数 | 缺失符号（最多 12 个）|
| --- | --- | --- |
| `bin/GPIO2SDIO.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/NsBkSet.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/SDIO2GPIO.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/SetPort.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/Wltestcmd` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/adjstctl.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/bk.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/bootin.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/bsa_server` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/btRFTest.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/btblog.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/crypter.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/dhd` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/gpioctrl.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/im.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/iperf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/launch_shell.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/mpr_monio.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/mpr_viewLog.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/mwf_monio.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/netblog.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/openssl` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/rcvcmd.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/scenario.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/sen.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/sndcmd.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/soundstretch` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/testcmd.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/ud_datcnv.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/ud_send_lsi.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/ujlog.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/ulogio` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/ulogio.elf` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/uuidgen` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/wl` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `bin/wpa_supplicant` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `lib/ipsec/lib/ipsec/libcharon.so.0` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `lib/ipsec/lib/ipsec/libstrongswan.so.0` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `lib/ipsec/lib/ipsec/libvici.so.0` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |
| `lib/ipsec/lib/ipsec/plugins/libstrongswan-kernel-netlink.so` | 2 | `_Jv_RegisterClasses`, `__gmon_start__` |

## 最普遍缺失的符号（Top 40）

| 符号 | 缺失它的文件数 |
| --- | --- |
| `_Jv_RegisterClasses` | 192 |
| `__gmon_start__` | 192 |

## 按前缀聚类

| 前缀 | 符号数 |
| --- | --- |
| `` | 2 |

