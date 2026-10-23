# WidgetSystem 的 Layer 注册表与"最小 Layer"

> 回答 QEMU 侧：`getLayer` 返回 0 之后崩在 0x18，该怎么补。

---

## 一、★ 结论：Layer 注册表**就是那个 `+0x484` 环形链表**

`WidgetSystem::getLayer(ID const&)`（`0x618D08`）内部：

```asm
618cb8: 取 WidgetSystem 单例
618cc6: r0 = *(ws)
618cc8: add.w r0, r0, #1152   ; +0x480
618ccc: adds  r0, #4          ; ★ +0x484 —— 正是你们初始化过自指头的那个链表
618cce: bl 0x6178F8           ; begin(head)
618cd4: loop:
618cd4:   bl 0x61790C          ; next / 取元素
618cda:   bl 0x6273B4          ; 从迭代器取 Layer*
618cde:   r1 = [id]
618ce0:   ID::operator==(layer+?, id)
618ce4:   命中 → 返回该 Layer
618cf2:   bl 0x618C88          ; advance，循环
```

⇒ **`getLayer` 就是遍历 `WidgetSystem + 0x484` 那个环形链表。**

**⇒ 只要链表里有 Layer，`getLayer` 就能找到 —— 而你们的自指头已经让这个链表"合法但为空"。**

## 二、★ Layer 怎么造：`createLayer` 是导出符号，且**会自己注册**

`WidgetSystem::createLayer(ID const&)`（`0x6171FC`，导出）：

```asm
6171fe: movs r0, #60 (0x3C)   ; ★ Layer 大小 = 0x3C
617204: blx   malloc
61720c: r1 = [id]             ; id 的第一个字
61720e: bl    0x6154B8        ; Layer 构造函数
617212: return r4             ; 返回 Layer*
```

`Layer::Layer(id)`（`0x6154B8`）：

```asm
6154c4: bl 0x627580           ; ★ 就是那个"往 ws+0x484 链表插入"的函数！
                              ;   ⇒ Layer 在构造时自动注册进注册表
6154ce: r3 = [GOT]+8
6154d2: str r3,[this+0]       ; +0x00 = vtable
6154d6..: ID 比较（两个已知 ID 走特殊分支）
6154f4: str r0,[this+0x34]    ; +0x34 = 资源指针（特殊分支）
6154f6: malloc(0x13C)
61550a: str r5,[this+0x38]    ; +0x38 = 子对象
615510: r0 = this + 0x18      ; ★ +0x18 是【内嵌子对象】，把它的地址传下去
615514: r1 = [this+0x38]
```

### Layer 布局（已确认）

| 偏移 | 宽 | 内容 |
|---|---:|---|
| `+0x00` | 4 | **vtable** |
| `+0x04` | 4+ | **ID**（`[id]` 第一个字） |
| `+0x18` | ? | **内嵌子对象**（以它的地址作为 `this` 传给 `0x8647DC`） |
| `+0x34` | 4 | 资源指针（特殊 ID 分支才设） |
| `+0x38` | 4 | 子对象指针（`malloc(0x13C)`） |
| 总大小 | **0x3C (60)** | — |

## 三、★ 推荐修法：interpose `getLayer`，为空就调 `createLayer`

```c
void *my_getLayer(void *ws, void *id) {
    void *layer = real_getLayer(ws, id);          // 0x618D08
    if (!layer) {
        printf("GETLAYER miss id=%08X%08X -> createLayer\n", ...);
        layer = real_createLayer(ws, id);          // ★ 0x6171FD（导出）
    }
    return layer;
}
```

**为什么这是最优解**：
1. `createLayer` 是**导出符号**，可以直接 `dlsym`
2. 它内部调用真正的**构造函数** ⇒ vtable、内嵌子对象、子对象全部正确
3. 构造函数会**自动把 Layer 插进 `+0x484` 链表** ⇒ 之后 `getLayer` 就找得到了
4. **不需要手工拼 Layer**，避开"零对象不够真"的老问题

## 四、为什么**不建议**手工拼一个"最小 Layer"

崩点 `0x6137DC: add r0,r0,#24` + `0x8647DC` 里 `ldr r3,[r0,#0]`
⇒ 要求 **`Layer+0x18` 处有一个合法的 vtable 指针**，否则立刻崩。

而 `Layer::Layer` 里 `+0x18` 被当作**内嵌子对象的地址**传下去（`add.w r0, r4, #24`），
说明它不是简单的一个字，而是一段结构。

⇒ 手工拼假 Layer 需要至少伪造：
- `+0x00` 的 vtable（且后续用到的槽都要有效）
- `+0x18` 起的内嵌子对象（其 `+0` 也要是 vtable）
⇒ **成本高且脆弱**；直接 `createLayer` 让真构造函数干活更省事也更稳。

## 五、要确认的一件事

崩点 `0x6137D4: blx WidgetSystem::getLayer(id)` 里的 **id 是哪个** —— 你们在 interpose 里打出来即可。
拿到 id 后：
- 若它是"某个固定视图 ID"，说明上层认为该 Layer 应该已经创建
- 可以先在进入 UI 之前**主动 `createLayer(该 id)`** 一次，把注册表填好
