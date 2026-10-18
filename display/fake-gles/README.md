# QEMU fake dmpgles2 / VIP display shim

这是 QEMU 专用模块，不用于真机。

它提供 major 248/minor 29 的 `/dev/dmpgles2`，记录 App 的 GPE ioctl，并提供软件 framebuffer。
在 CXD90045 QEMU 中，模块还可把 320x240 RGBA4444 测试帧写入 DDR0 `0x01000000`，配置 VIP
layer2；GTK QEMU 的 QMP screendump 已实测得到彩色菜单样式图像。

已实测 ioctl：

```text
0x80048206
0xc0048202
0x80048263
```

真实显示链：

```text
fake_gles.ko → VIP layer2 → bionz_vip 320x240 console → GTK/QMP
```

下一步是把测试帧替换为 App GPE 的真实像素提交，并接入菜单输入。
