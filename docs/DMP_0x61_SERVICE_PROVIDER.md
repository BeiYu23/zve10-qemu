# `0x61003C` 这个 DMP 服务是谁提供的：**`grm_ma.ko`（被我们 stub 掉了）**

> 新断言：`int_cmd.cpp:169 dmpNative::InternalCmd::alloc_sync():
> Assertion ((0x61<<16)|0x3C) == osal_get_msg_src(handle) failed`
> ⇒ 向 DMP 显存服务发请求，回来的应答源 id 不是 **0x61003C**。

---

## 一、先纠正一件我说错的事：a7c2 **没有** `dmp-service.c`

| 事实 | 说明 |
|---|---|
| a7c2 的 `runtime/analysis/` 里 | **只有 `dmm-service.c`**，没有 dmp-service |
| 全仓库搜 `0x61xxxx` | **零命中** |
| 他们注册的同步队列 | `0x8280074d` / `0x8280084d` / `0x8288076c`（OSD 相关），**不是 0x61** |

⇒ 我之前提"dmp-service.c"是**记混了**，向 QEMU 侧道个歉。
⇒ **`--dmm-service` 与 `--dmp-service` 不是两个概念**：只有 DMM service，
**DMP 没有用户态服务，因为它是内核模块提供的。**

## 二、★ 谁是 `0x61xxxx` 的提供者：看 UIPC 角色就能确定

| 模块 | 导入的 osal 接口 | 角色 |
|---|---|---|
| **`grm_gles.ko`** | `osal_snd_msg` / `osal_snd_sync_direct` / `osal_snd_sync_msg` / `osal_valloc_msg_wait` | **客户端**（只发不问） |
| **`grm_ma.ko`** | **`osal_reg_sync_msg_queue_cb`** / `osal_rcv_msg_tmo` / `osal_snd_msg` / `osal_free_msg` / `osal_unreg_msg_queue_cb` / `osal_valloc_msg_wait` | **★ 服务端**（注册队列 + 收消息） |
| `dmm.ko` / `backup.ko` / `upm.ko` | 各 7 个（reg_msg_queue_cb + reg_sync + rcv + snd） | 服务端 |

**`grm_gles.ko` 是 `author=DMP/Sony`、创建 `/dev/dmpgles2` 的 DMP GLES 驱动，
它的分配请求就发给 `grm_ma.ko` 注册的那个同步队列。**

⇒ **`0x61003C` 这个子系统就是 `grm_ma.ko` 提供的。**
⇒ 而我们的 stub 列表里**正好有 `grm_ma`** ⇒ 没有 0x61 服务 ⇒ 应答源 id 不符 ⇒ 断言。

## 三、所以：**不要去写 0x61 服务 shim**

那等于把 `grm_ma.ko` 用用户态重写一遍（MA 分配器 + 显存池 + 队列协议），
工作量和风险都远高于直接加载真模块。

**正解仍是上一轮那条：恢复真实内核模块。**

a7c2 的加载序列（原文）：
```sh
insmod /usr/kmod/dmm.ko   DmmConfig=/setting/DmmCfg.bin
insmod /usr/kmod/grm_ma.ko          # ← 0x61 服务提供者
insmod /usr/kmod/backup.ko Backup_load=1 Backup_save=0
insmod /usr/kmod/stream.ko ; insmod /usr/kmod/stream2.ko
insmod /usr/kmod/utimer.ko
insmod /usr/kmod/osal_ulogio.ko ulogio_initfile=/setting/ulogio.bin
insmod /usr/kmod/devno.ko / osal_utm.ko / osal_uipc.ko
```

> 注意：a7c2 的列表里**没有 `grm_gles.ko` 的显式 insmod** —— 它应该来自原厂
> `initrc/insmod_drv2` 那 38 个模块之一。所以**要确认我们的 QEMU 把 `grm_gles` 也 insmod 了**。

## 四、你加的两个 interposer 会带来副作用（建议用完即撤）

```c
int dmp_allocator_lib_init(void){ return 0; }   // 只跳过初始化
int dmp_allocator_mmap_init(void){ return 0; }
```

`dmp_allocator_lib_init` 只 `osal_wai_sem_tmo` 后就断言 —— 返回 0 只是**跳过了初始化**，
后面 `alloc_sync` 仍然要真服务 ⇒ 失败只是**后移到下一处**（正是现在看到的）。

⇒ **真模块加载后，这两个 interposer 应一并撤掉**，否则分配器上下文永远是空的，
后面的 DMP 分配会拿到空池。

## 五、给 QEMU 侧的检查清单

1. `/proc/osal/uipc` 里**有没有 0x61 子系统**（应为「没有」—— 因为 grm_ma 是 stub）
2. **`grm_ma.ko`、`grm_gles.ko` 是否真的被 insmod 了**（`lsmod` 或 `/proc/modules`）
3. 恢复真 `grm_ma` 后再看 `/proc/osal/uipc` ⇒ 应出现 0x61 子系统 + 客户端
4. 然后 `dmp_allocator` 的两个 interposer 撤掉，看 line 108 断言是否**自然**通过
