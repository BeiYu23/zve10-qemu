# 轮次 29：/setting 的硬件绑定文件确认（DmmConfig 等）

## 一、/setting 引用者的模型无关性

逐文件检查"谁引用它"，两机型结果完全一致：

| 文件 | 引用者（两版相同） | 结论 |
|---|---|---|
| `appfw_infra/` | `libObj.so` `libsencore.so` | 模型无关 |
| `DmmConfig.bin` | `libIMDB.so` | 模型无关（路径条目） |
| `psd.bin` | `CameraOnDemandSaver.so` | 模型无关 |
| `kemco.txt` | `libGetLog.so` | 模型无关 |
| `.etools` | `libsencore.so` | 模型无关 |
| `ulogio.bin` | 无人引用 | 无影响 |

**⇒ 全部模型无关，E10 的 /setting 文件结构对两版都成立。**

## 二、DmmConfig.bin 是硬件绑定，保持配对

```
E10 的 DmmConfig.bin : 78,120 B
  头部: 00×16, 00 01 01 01 01 01 00…（配置表：零字节 + 0x01 标志位）
  可读串: 大量 "0000…" 与 "````…" 填充分段

相关内核驱动（两版内容不同）:
  dmm.ko         E10=85,588   M2=85,588
  mcmn_drv.ko    E10=116,848  M2=146,840
  grm_gles.ko    E10=15,648   M2=15,648
  grm_ma.ko      E10=24,800   M2=24,800

⇒ 我们【保留 E10 的全部 kmod】+【保留 E10 的 DmmConfig】
⇒ 配对关系正确、自洽
⇒ 而 M2 的 appFw / libObj 都不直接解析它（只有 libIMDB 把它当路径字符串）
⇒ 风险：低
```

## 三、顺带发现：appFw 的 SD 卡设置路径（无影响）

```
appFw.so 里的字符串:
  %c:%s/SONY/SETTING/%s/CAMPR%02d%c.DAT
  %c:%s/SONY/SETTING/%s/CAMSET/%s.DAT
  %c:%s/SONY/SETTING/%s/FTPSET/%s.DAT
⇒ 这是"把相机设置保存到 SD 卡"的功能路径，与启动无关
```

## 四、静态分析到此的完整清单（14 个维度）

```
 1. 文件系统完整性        e2fsck / 几何 / UUID / 特性位
 2. 符号闭包              undef 符号全部可解析
 3. 传递依赖              224 个二进制零缺失
 4. 版本正确性            业务库全 M2 版
 5. 目录完整性            M2 的 /usr 全覆盖（17 个缺口全有正当理由）
 6. 内存                  两机型 Linux 均 141.7 MB
 7. 资源格式              global.xdb / area_check_data 同代同格式
 8. ABI                   glibc 上限 / 浮点 ABI / 零外部调用
 9. loop 支持             内核有、系统自己在用 loop6/7
10. CP 固件               同代同协议族（差 0.54%）
11. 显示通路              M2/E10 同一个 /dev/dmpgles2
12. 同步消息              系统公共 id + 无回调不阻塞
13. 符号解析链            含加载次序，且是 M2 原厂路径
14. /setting 配置兼容    必需项齐全，缺失项符合"缺失即默认"设计
```

## 五、结论

```
静态分析 14 个维度全部完成，全部指向"可行"。
离线实证 7 项全部通过（ld.so / 内核 / 脚本 / 诊断 / 安全 / 刷写 / 部署）。

⇒ 已无新的静态可查项。下一步必须是真机。
```
