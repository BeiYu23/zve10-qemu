# QEMU fake dmpgles2

这是 QEMU 专用的 `grm_gles.ko` 替代模块，不用于真机。

它注册 major 248/minor 29 的 `/dev/dmpgles2`，提供：

- `open`：建立 private data；
- `ioctl`：记录命令和用户参数前 8 个 u32，返回成功；
- `mmap`：提供 64KB 清零 framebuffer。

ZV-E10 App 已实测执行：

```text
0x80048206  {0,0,0,0,...}
0xc0048202  {1,0,0,1,0,0,0,0}
0x80048263  {0,0,0,0,...}
```

原 `grm_gles.ko` 会在 `sugilite_open+0x28` 执行 `ldrex/strex`，QEMU 路径触发非法指令；fake 模块已成功绕过该硬件入口并让 App 进入 `CmnViewMenuData` 菜单数据构造。
