I was wrong: the real fill path does block, and this makes interposing WriteLockBits mandatory

The porting side was right that my scan missed an indirect call. Following it changes the conclusion.

At 0x118B9C inside the fill routine, the code sets r1 to 0xFFFFFFFF and r2 to zero, then calls
GRM_bitmapWriteLockBits through the PLT at 0x118BA2. Tracing that argument through the
implementation at 0x11BB68 shows it takes the blocking branch: the comparison at 0x11BB94 matches
0xFFFFFFFF, the helper at 0x11BAF4 is called, and then execution reaches the pthread_cond_wait at
0x11BBA8 on the condition variable at bitmap+0x40.

So the real gpermFillrect is synchronous. It waits inside WriteLockBits for the GPU to signal
completion. My earlier claim that it is asynchronous was wrong, and it was wrong because the grep I
used filtered for blocking primitives appearing literally inside the two functions, which cannot see
a blocking primitive reached through a PLT call. The question I answered was narrower than the one I
asked.

The consequences flip. The porting side original reasoning was correct: replacing gpermFillrect does
remove a real wait. And more importantly, if the shim replaces gpermFillrect but any other code path
still calls GRM_bitmapWriteLockBits with minus one, that path will hang forever with no output.
Interposing GRM_bitmapWriteLockBits and BMP_WriteLockBits to neutralize the blocking branch is
therefore required rather than optional.

CheckUpdateFinished remains necessary, but as a separate mechanism: it polls VDF status rather than
waiting on the bitmap condition variable.

## 附：`GRM_bitmapWriteLockBits` 调用点的**精确**统计

移植侧用"`mov.w r1,#0xFFFFFFFF` 出现 182 处"作为代理指标。按我们确立的方法论
（用直接事实代替间接指标），实际统计了**真正的调用点**：

**`libObj.so` 内 `blx GRM_bitmapWriteLockBits@plt`（`0x3B588`）的全部调用点 = 4 个：**

| 调用点 | arg1 | arg2 | 是否阻塞 |
|---|---|---|---|
| `0x3E586` | **`0xFFFFFFFF`** | `r7+4`（有效 out） | **是** |
| `0x1175DE` | **`0xFFFFFFFF`** | — | **是** |
| **`0x118BA2`** | **`0xFFFFFFFF`** | `NULL` | **是**（★ 填充路径） |
| `0x229FF0` | `r9`（运行时值） | `r7+176`（有效 out） | 取决于 r9 |

### 结论

**4 个调用点里有 3 个确定会走 `pthread_cond_wait` 阻塞分支，其中就包括绘制路径（`0x118BA2`）。**

**⇒ interpose `GRM_bitmapWriteLockBits` 不是防御性建议，而是必做项。**

（182 是"传 -1"这一模式的总体出现次数，是**代理指标**；4 是实际调用点数，是**直接事实**。
后者才是结论依据。）
