# ZV-1M2 混合镜像的库 ABI 审计

> 问题：混合镜像 `lib/` 有 208 条目（146 文件 + 58 符号链接 + 4 子目录），
> 其中 16 个库"与 ZV-1M2 同大小但内容不同"，构建器保留了 ZV-E10 版。
> **审计结论：16 个全部安全，镜像可直接使用。**

## 一、6 个"仅 ZV-E10 有、ZV-1M2 没有"的库 —— 全部合理

| 库 | 大小 | 判断 |
|---|---:|---|
| `libInfraCameraAllSetting.so` | 68,648 | 相机设置项（机型相关）✅ |
| `libInfraCameraProfile.so` | 51,916 | 相机配置（机型相关）✅ |
| `libInfraLensCommunicator.so` | 8,196 | **镜头通信**（E10 可换镜头，ZV-1M2 内置）✅ |
| `libInfraExtTransferApp.so` | 10,148 | ✅ |
| `libclema.so` | 77,852 | ✅ |
| `githash` | 67 | 非库，构建标记 |

## 二、16 个"同大小不同内容"的库 —— 逐个审计

**成因**：两版大小完全一样但字节不同（编译时内嵌了不同机型常量/字符串）。
构建器按"同名同大小 ⇒ KEEP_E10"保留了 E10 版。

### 2.1 导出符号完全相同（12 个）⇒ ABI 兼容，零风险

```
libInfraExtBackupCommunicator.so   libInfraExtCmdSvc.so
libInfraKikiLogSender.so           libInfraNetworkServiceInfo.so
libNetContUtil.so                  libdig_level.so
libgps_rsrc.so                     libmintx.so
libosal_uipc.so                    libosal_ulogio.so
libptp.so                          libtestcmd.so
```

### 2.2 符号有差异的 4 个 ⇒ 逐个查明均无害

**① `libNetCommon.so`**（appFw 的 DT_NEEDED，最需关注）
```
M2 多一个重载:
  NetCommonLibrary_::ContentLocator::SetResourceData_AVCHD(MdbHandle, RsrcType, unsigned)
```
**⇒ appFw 对该符号引用次数 = 0** ⇒ 新重载无人使用，保留 E10 版安全。

**② `libInfraMscServer.so`**（唯一有真实 ABI 变化）
```
E10: UsbSyncFreePhysBuffer(u16, u16)     M2: (u16, u32)      ← t(2B)→j(4B)
E10: UsbSyncAllocPhysBuffer(u16,u32,u16*,u32*,u32)
M2 : UsbSyncAllocPhysBuffer(u16,u32,u32*,ulong*,u16*)
```
**⇒ 全 M2 201 个库中，这两个符号【只被 libInfraMscServer.so 自己引用】（各 1 次 = 定义）**
**⇒ 无外部调用者 ⇒ ABI 变化是内部细节，保留 E10 版安全。**

**③④ `libWlanAdjust.so` / `libWlanResource.so`**
```
E10: NetworkTraceRecord(u8,u8,u16,m)   ← unsigned long
M2 : NetworkTraceRecord(u8,u8,u16,j)   ← unsigned int
```
**⇒ ARM32 上 `long` 与 `unsigned int` 同为 4 字节 ⇒ 同宽，ABI 无影响。**
**⇒ 且这两个是 Wi-Fi 硬件绑定库（nvram 选择），保留 E10 版反而更正确。**

## 三、结论

**16 个库全部不需要替换，镜像可直接部署。**

## 四、preload 时序问题与备选方案

**问题**：`libIMDB.so` 是 `im.elf` 的 DT_NEEDED，启动时即被映射并重定位。
任何 preload 库的构造函数跑起来时，老 `libIMDB` 已经映射完 ⇒ 事后 bind 无效。

**⇒ 必须用 `LD_PRELOAD` 直接提供 M2 版 libIMDB。**

**方案 A（需 bootin 支持冒号分隔）**：
```
preload = /log/zvegisk/m2/libIMDB.so : /setting/zvegisk/zvegisk.so
```

**方案 B（推荐，只需一个 preload 条目）**：
**在 M2 的 `libIMDB.so` 里加一个 `.init_array` 构造函数，让它在自身初始化前做 loop mount + bind。**

```
1. 复制 M2 的 libIMDB.so
2. 追加 do_bind() 代码（loop mount + mount --bind /usr/lib、/usr/share/app）
3. 在 .init_array 插入指向 do_bind 的指针
4. preload 只写这一个文件
```
**⚠️ `do_bind()` 必须用直接 syscall，不能依赖库函数（此时 libc 可能未就绪）。**

## 五、传输校验重点（6 个致命项）

| 路径 | md5 |
|---|---|
| `lib/appFw.so` | `f079cda0633b6e0a990bb46d4028f0b4` |
| `lib/libObj.so` | `738a23316b0b8689370b0a8a1d5636d7` |
| `lib/libIMDB.so` | `91eca62f9cae6ee2e7ee34c31002a968` |
| `lib/appFwBoot.so` | `bb4cdbabc23194998c97a6b4cfe8b629` |
| `share/app/style_cmn.uxc` | `b26bd4864468764ba07c4b3c7f9354ba`（schema 4）|
| `share/app/global.xdb` | `d9505dfa49dead95b7665d98eddeeaba` |
