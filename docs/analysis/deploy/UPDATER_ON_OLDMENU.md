# 老菜单（ZV-E10）其实有完整更新机制 —— 只是没有 UI 入口

> 用户观察正确：**只有新菜单有「设置→版本→软件更新」菜单项**。
> 但**更新机制的每一个组件 ZV-E10 都有**，触发方式是「标志文件 + LSI 通知」。

---

## 一、ZV-E10 上更新的全部组件（一个不少）

| 组件 | ZV-E10 | ZV-1F | 说明 |
|---|---|---|---|
| `libupdaterufp.so` | ✅ 13,152 B | ✅ | **处理 BODYDATA.DAT** |
| `libupdatertalk.so` | ✅ | ✅ | 导出 `KeyArray` / `Dec_Scramble`（AES 解密） |
| `libupdatermedia.so` | ✅ | ✅ | 存储介质访问 |
| `libupdatercommon.so` | ✅ | ✅ | 文件/标志/挂载工具 |
| `libupdaterufunc.so` | ✅ | ✅ | |
| **`/usr/bin/up.sh`** | ✅ **299 行** | ✅ | **★ 更新模式控制脚本** |
| **`/usr/bin/crypter.elf`** | ✅ 39,084 B | ✅ | **解密并写入固件** |
| `ud_datcnv.elf` | ✅ | ✅ | 读写 dat2/dat3/dat4 |
| `ud_send_lsi.elf` | ✅ | ✅ | **通知 LSI 切换模式** |
| libIMDB 服务注册 | ✅ 完全相同 | ✅ | `udtr_ufp_init/exit`、`InfraUpdaterDPro_*` |

**⇒ 机制齐全，只缺菜单项。**

---

## 二、★★★ `up.sh` 的模式表（Sony 原厂脚本）

```sh
UPDIR=/setting/updater
UPMODE=$UPDIR/mode
UPMODE1=$UPDIR/mode1
```

| 调用 | 标志文件 | 模式 |
|---|---|---|
| **`up.sh`（无参数）** | **`mode` + `mode1`** → **"Updater mode"** ★ | mode_num=4 |
| `up.sh user` | 只留 `mode` | 用户模式 (2) |
| `up.sh serv` | `mode` + `mode3` | 服务模式 (3) |
| **`up.sh ver`** | **`mode` + `mode6`** | **★ 版本跳过模式 (6)** |
| `up.sh ms` | `mode` + `mode5` | MS Crypt 模式 |
| `up.sh modmod <8位>` | — | **★ 改机型**（nflasha1 的 `dat2`） |
| `up.sh modver <4位>` | — | **★ 改版本**（`/setting/updater/dat4`） |
| `up.sh modreg <8位>` | — | **★ 改区域**（`dat3`） |

### 2.1 无参数时的核心逻辑

```sh
if [ -f "$UPMODE" ]; then
    rm -f $UPDIR/mode*          # 已存在 → 清除 → 回正常模式
else
    touch $UPMODE $UPMODE1      # 不存在 → 创建 mode + mode1 → 下次启动进更新模式
fi
sync
echo "next boot...   Updater mode"
ud_send_lsi.elf $mode_num        # ★ 通知 LSI 切换（mode_num=4）
```

**⇒ 触发链：`/setting/updater/mode` 存在 → 下次启动进 Updater 模式 → 处理 SD 卡的 BODYDATA.DAT。**

---

## 三、★★★ 对移植的直接价值：三道关卡都能绕过

| 关卡 | Sony 自带工具 | 用法 |
|---|---|---|
| **机型不匹配**（"更新文件版本不正确"） | `up.sh modmod <8位>` | 改 `nflasha1` 的 `dat2` 里的机型值 |
| **版本不允许**（降级/同版本） | **`up.sh ver` ⇒ `mode6`** | **版本跳过模式** |
| 区域 | `up.sh modreg <8位>` | 改 `dat3` |

**⇒ 我们之前担心的"跨机型固件装不进去"—— Sony 自己提供了绕过工具。**

`ud_datcnv.elf -d/-e -i <in> -o <out> -a` 是读写这些 dat 的加解密工具。

---

## 四、数据层流程（反汇编确认）

`udtr_get_firm_info_inmedia(out_info, media_path)`：

```asm
Fsys_Open(media_path, O_RDONLY, &fd)      ; 打开 BODYDATA.DAT
udtr_cmd_search_chank(fd, &chunk_off)     ; 搜索 chunk 头
Fsys_Read(fd, buf, 0x204)                 ; 读 0x204 字节头
loop i: Dec_Scramble(buf + i*16)          ; ★ 每 16 字节解密一次（AES 块）
→ 解析出 firm_model / firm_version
```

**`libupdaterufp` 导出的完整接口：**
```
udtr_ufp_init / udtr_ufp_exit
udtr_get_firm_info_inmedia     ← 从 media 读固件信息（上面的流程）
udtr_write_firm_from_media     ← ★ 从 media 写固件
udtr_cmd_search_chank          ← 搜索 chunk
udtr_alloc_dmm / free_dmm / copy2dmm
udtr_get_file_size / conv_stat2err
```

---

## 五、⇒ 三条可行路线

### 路线 A（★ 最安全）：fastboot 只写一个标志文件，其余交给 Sony

```
① fastboot 写 /setting/updater/mode（0 字节文件）+ mode1
   （若也要跳版本，再加 mode6）
② SD 卡根目录放 BODYDATA.DAT（我们定制或官方的）
③ 正常启动 → 相机自己进 Updater 模式 → Sony 官方流程刷写
```

**⇒ fastboot 只介入"建一个空文件"，真正的刷写、校验、恢复全部由 Sony 代码完成。**
**⇒ 出错时 Sony 的 updater 有恢复逻辑，比裸写分区安全。**

### 路线 B：拿到 shell 后直接跑 `up.sh`

```
/usr/bin/up.sh          # 进更新模式
/usr/bin/up.sh ver      # 版本跳过
/usr/bin/up.sh modmod 0x1030010
```

**⇒ 需要 shell。老菜单 telnet 失败过，但新菜单内置 SSH。**

### 路线 C：fastboot 直接写 /usr（原计划）

**⇒ 最直接，但没有 Sony 的校验与恢复保障。**

---

## 六、结论

**「老菜单没有 SD 卡更新功能」是对的 —— 但那是 UI 层缺失，不是机制缺失。**

**ZV-E10 上 `up.sh` / `crypter.elf` / `ud_datcnv.elf` / `ud_send_lsi.elf` / 全部 updater 库都在，
而且 `up.sh` 还自带「改机型 / 改版本 / 版本跳过」三个后门。**

**⇒ 我们不需要自己发明刷机方法 —— Sony 把工具留在机器里了。**
