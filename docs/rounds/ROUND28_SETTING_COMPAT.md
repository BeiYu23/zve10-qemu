# 轮次 28：/setting 配置兼容性分析（之前完全没查过的区域）

## 一、为什么查这个

新 UI（M2 的 appFw）会读 `/setting` 里的配置。如果它期望某些 E10 没有的文件，
可能在启动时失败。而 `/setting` = nflasha2 是可写分区，我们之前只碰过
`mode/dmode` 和 `mode/preload` 两个文件，没做过整体对照。

## 二、M2 组件请求的 /setting 路径（从二进制提取）

```
appFw.so      : acctime1.bin  acctime2.bin  facedmy.bin  facerec%d.bin
                sen/smode     sig.txt
bk.elf        : Backup.bak    Backup.bin
bootin.elf    : mode/preload
im.elf        : sen/smode
libObj.so     : .hasp  gps  gps/assistme.dat  mode  mode/cmode
                sen  sen/  sen/smode
                updater  updater/dat4  updater/mode  mode1  mode3  mode6
sen.elf       : .hasp  sen/  sen/smode
```

## 三、E10 的 /setting 实际内容（工厂镜像）

```
.etools/  .hasp(88)  Backup.bak(1,235,836)  Backup.bin(1,235,836)
DmmConfig.bin(78,120)  appfw_infra/  facedmy.bin(51,200)  facerec0.bin(57,888)
gps/  kemco.txt(1,925)  mode/{dmode,preload,zve10_net}  psd.bin(4,820,992)
ulogio.bin(3,796)  updater/dat4  zsd(4)  zvegisk/  zvping.log(34)
```

**缺失的只有三个：`acctime1.bin`、`acctime2.bin`、`sig.txt`**

## 四、判断：缺失无害

### 证据 1：这套系统本身就以"配置缺失"为常态

```
E10 自己的 /setting 里也有大量"应有但缺失"：
  /setting/sen/      空目录，但两版都读 /setting/sen/smode
  /setting/gps/      空，但读 gps/assistme.dat
  /setting/updater/  只有 dat4，但读 mode/mode1/mode3/mode6
  /setting/mode/cmode 不存在，但 libObj 会读

⇒ 说明设计上就是"读不到就按默认值走"，这是原厂行为模式
```

### 证据 2：这三个文件在 E10 全部组件里零引用

```
acctime1 / acctime2 / sig.txt 在 E10 的 lib/*.so 和 bin/*.elf 里搜索：全部 0 命中
⇒ 是新 UI 独有的功能文件（使用时长统计 / 签名校验）
⇒ 且 /setting 可写，app 首次运行时会自己创建
```

## 五、结论

```
/setting 兼容性：良好
  必需的配置（.hasp / Backup.* / facedmy / facerec / gps / mode / updater）全部存在
  缺失的三个是新 UI 独有的可选文件，符合系统"缺失即默认"的设计模式
风险等级：低

⇒ 无需为 /setting 做任何移植工作
```

## 六、顺带确认的一件事

```
/setting/mode/ 现有: dmode(1B='3') preload(0B) zve10_net(4B)
⇒ 我们的 deploy 脚本会写 dmode='3e' 和 preload=三项列表
⇒ 这两个文件就是 bootin 读取并传给 im.elf 的通道，已验证
```
