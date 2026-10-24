# tools/

## exidx_which.py — 地址 → 所属函数（权威）

用 `.ARM.exidx` 回答"某地址属于哪个函数"。**不需要符号表**，由编译器生成，
是 ARM EHABI 规定的函数范围来源。

### 为什么需要它

反汇编时判断函数边界，常见两种方法都不可靠：

| 方法 | 问题 |
|---|---|
| `dynsym` 的 `st_size` | 可能被截断（如 `clear` 声称 16 字节，实际是纯尾调用，确实 16 但很多函数不是） |
| "下一个命名符号" 当边界 | 中间夹着**无符号 static 函数**时严重高估（实测把 826 字节高估成 5434） |

`.ARM.exidx` 的规则：**每个函数由下一条目的起点界定**。

### 用法

```bash
export PATH=<toolchain>/bin:$PATH

# 查若干地址
python3 tools/exidx_which.py /path/to/libObj.so 0x117DF6 0x108E1E

# 列出某范围内的所有函数起点
python3 tools/exidx_which.py /path/to/libObj.so --list 0x117000 0x118500
```

### 实测输出（ZV-1F libObj）

```
地址         函数起始       函数结束       长度      符号
0x108E1E  0x108C84  0x108E4C  456     （无名）
0x108C52  0x108C52  0x108C62  16      GraphicsImage::clear
0x1178A4  0x1178A4  0x1179DE  314     GRM_gpermFillrect
0x1179DE  0x1179DE  0x117D18  826     GRM_gpermRectblit
0x117DF6  0x117D18  0x117E34  284     （无名）
0x117F0A  0x117E34  0x117F3C  264     （无名）
```

### ★ 附带结论

`GRM_gpermFillrect` 两版固件都是 **314 字节**，`GRM_gpermRectblit` 都是 **826 字节**
（ZV-1F 与 ZV-E10 一致）⇒ **GPE 层代码相同 ⇒ 一个 shim 可同时服务两条线。**
