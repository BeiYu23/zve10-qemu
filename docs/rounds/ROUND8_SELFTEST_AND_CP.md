# 轮次 8：一次开机给全部判据 + CP 固件兼容性评估

## 一、bindm2 升级为自诊断工具（6,016 B）

一次开机后 `/log/bindm2.log` 会包含：

```
=== bindm2 start ===
loopno=N
LOOP_SET_FD=0                 ← 0 成功；负数 = errno
mount ext2=0
ext2 mounted
bind /usr/lib=0
bind /usr/share=0
-- after bind --              ← ★ 新增：bind 之后这些文件应变成 M2 版
libObj.so      = 3931624      （E10 是 21305528）
appFw.so       = 64273528     （E10 不存在 → -1）
appFwBoot.so   = 8312         （E10 不存在 → -1）
libIMDB.so     = 40596        （E10 是 37204）
libInfraSKD.so = 7259708      （E10 是 10184）
global.xdb     = 16112        （E10 是 12252）
im.elf ...                    ← 从 /proc/self/cmdline 提取的 argv
MemFree: NNNN kB
=== bindm2 done ===
```

**判读逻辑：**

| 现象 | 结论 |
|---|---|
| `appFw.so = 64273528` | **bind 完全生效**，M2 库就位 |
| `appFw.so = -1` | bind 没生效，或镜像里没这个文件（镜像不对） |
| `mount ext2 = -19` | 镜像不是有效 ext2（上传不完整/文件损坏） |
| `LOOP_SET_FD = -1` | 无权限或 loop 设备问题 |
| 日志里连 `=== bindm2 start ===` 都没有 | im.elf 没跑到 preload（或 /log 还没挂载 → 看 /tmp/.bindm2.log） |

**⇒ 一次开机就能区分「bind 失败」和「bind 成功但 app 层跑不通」。**

## 二、CP 固件兼容性评估（av-cam.bin）

```
E10  av-cam.bin : 17,289,388 B  md5 cdcae9d4…
M2   av-cam.bin : 17,195,164 B  md5 f8a0c402…
长度差          : 94,224 B = 0.54%
64 字节块不同率 : 72.6%
随机块相同率    : 25.9%
相同前缀        : 72 B（几乎从头就不同）
```

**但字符串层面高度一致：**
```
[ADF]AdfSetReverseLrCtrl SetAipStartParam err %x
[ADF]SetReverseLR ExecuteComp err %x
[ADF]SetReverseLR onOff %x
[ IN]SetReverseLr
basic_filebuf::_M_convert_to_external conversion error
Key has been revoked
⇒ ADF / AIP / LR 等成像子系统函数名、错误码格式完全相同
```

### 结论

```
字节层面 72.6% 不同、长度只差 0.54%、字符串完全对应
⇒ 典型「同一份源码、不同机型参数 + 重新编译」的特征
⇒ 成像【协议族】应当兼容，差异在机型参数（传感器/镜头/ND/EVF 表）

这对移植是【好消息】：
  M2 的 appFw 与 E10 的 CP 之间协议冲突的概率显著降低
  真正会不工作的应该是"机型特有功能"（ND、变焦、EVF、卡口），
  而不是整个成像链路
```

### 附：/system 下其它 CP 固件

| 文件 | E10 | M2 | 关系 |
|---|---:|---:|---|
| `ldr_drv.bin` | 7,012 | 7,012 | **相同** |
| `bonobo.bin` | 172,144 | 172,144 | 同大小、内容不同 |
| `wole_app.bin` | 10,848 | 11,180 | 不同 |
| `lif_app.bin` / `dfe_app.bin` / `dfe_dat.bin` | 有 | **无** | M2 不带这三个 |

**⇒ 我们保留 E10 的整个 `/system`，所以 CP 侧完全一致、自洽。**

## 三、本轮结论

```
静态分析到此已经把所有能确定的都确定了：
  · 启动链各环节的符号/依赖/版本 → 全部就绪
  · bind 方案的技术前提（loop、SONAME、mount 权限）→ 全部成立
  · CP 固件 → 同代同协议族，冲突概率低
  · 唯一的未知 → 真机跑一次才知道

⇒ 下一步必须动真机。工具已就绪：
    python3 /root/fw/oss/tools/bindm2/deploy_bindm2.py --image /mnt/build/nflasha15_v9.ext2
    重启 → 看屏幕 + 读 /log/bindm2.log
```
