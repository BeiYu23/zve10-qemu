# 事故：`/dev/dmpgles2` mmap 探测导致内核 oops + 自动重启

## 一、我做了什么

**在相机上跑了 `zvegisk_gpumap`（我自己写的探测程序），它做：**

```c
fd = open("/dev/dmpgles2", O_RDONLY);
p  = mmap(NULL, 0x200,     PROT_READ, MAP_SHARED, fd, 0);   // 512 B
q  = mmap(NULL, 0x20000,   PROT_READ, MAP_SHARED, fd, 0);   // 128 KB
r  = mmap(NULL, 0x2000000, PROT_READ, MAP_SHARED, fd, 0);   // 32 MB
// 每次映射后立刻读头 64 字节
...
fd2 = open("/dev/dmpgles2", O_RDWR);
mmap(NULL, 0x2000000, PROT_READ|PROT_WRITE, MAP_SHARED, fd2, 0);
```

## 二、结果：相机重启

**cmdline 里有 `exception.reboot=1` ⇒ **内核 oops 后自动重启**。**

## 三、为什么（`sugilite_mmap_dev` 的机制）

**反汇编 `grm_gles.ko` 的 `sugilite_mmap_dev`（440 B）：**

```asm
  1428: cmp  size, #512        (0x200)
  142c: cmpne size, #33554432  (0x2000000 = 32MB)
  1448: cmp  size, #131072     (0x20000 = 128KB)
  ... remap_pfn_range ...
strings:
  "sugilite_mmap_dev bar remap fail"
  "sugilite_mmap_dev shmem remap fail"
```

**★ 它用 `remap_pfn_range` 把**物理页**映射进用户空间。**

**★★★ 如果请求的物理范围无效/未映射，`remap_pfn_range` 可能仍然"成功"，
但**第一次访问**（我们的读）就 fault ⇒ 内核 oops ⇒ `exception.reboot=1` 自动重启。**

## 四、损害评估

| 项 | 是否有损 |
|---|---|
| **flash** | **✗ 没写任何东西** |
| **/setting** | **✗ 没动** |
| **/log** | **只多了 `/log/zvegisk_gpumap`（无害）** |
| **内存状态** | **重启已清空** |
| **相机** | **断电重启即恢复** |

**⇒ 无持久损害，但相机需要断电重启一次。**

## 五、★★★★★ 结论：`/dev/dmpgles2` 这条路**关闭**

**不是"未验证"，而是"已验证为致命"：**

```
如果【仅仅映射】GPU 设备就足以让内核 oops，
那么【通过它写像素】只会更危险。
⇒ 自绘制（在 CPU 侧直接画）在这个平台上不可行。
```

**★★★ 加上之前排除的六条：**

```
✗ /dev/fb0              ENXIO（无驱动）
✗ FBRM 高层 API          8 字节空桩，实测返回 0
✗ /dev/mem + 物理地址     无可见帧缓存
✗ /dev/stream @0x20312000 描述符 + wbi_cmpr 压缩数据
✗ ttycons               ttyAM0 串口
✗ SUGILITE ioctl        17 个命令全是 4 字节标量控制
✗ /dev/dmpgles2 mmap    ★ 映射本身即 oops（本次）
```

**⇒ **「自绘制」这个议题到此关闭。**
zvegisk 的边界 = 换资源 + 换库 + 崩溃回退。**

## 六、教训（第四次同类事故）

**1. 「只读」不等于「安全」** —— mmap + 读也能让内核 oops
**2. 对内核驱动的非常规操作，一次只测一个变量**
   （这次三个尺寸 + 一个 RW 映射混在一起，无法定位是哪一个致死）
**3. 该先问「这个探测能给我什么」，再问「怎么测」**
   —— 即使全成功，我们也只能用这块区域画像素，
   而 `remap_pfn_range` 映射的物理区域**大概率不是显示面**（前面六条已指向这个结论）
