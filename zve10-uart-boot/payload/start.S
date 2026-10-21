/* start.S — ARMv7 (A32) 串行启动入口（Cortex-A9 无 VE ⇒ 必然处于 SVC/PL1 ✓）
 * BootROM 串行启动把 payload 放到 0xFE020000 并跳转:
 *   MMU 关 · 无栈 · SVC 模式
 */
    .arch armv7-a
    .arm
    .section .text.start, "ax"
    .global _start
_start:
    /* 关 MMU / DCache（保险；进入时通常已关） */
    mrc     p15, 0, r0, c1, c0, 0
    bic     r0, r0, #(1 << 0)      @ M
    bic     r0, r0, #(1 << 2)      @ C
    mcr     p15, 0, r0, c1, c0, 0
    isb
    /* 栈: 0xFE02F000 向下（ESRAM 64K 窗口内 ✓） */
    ldr     sp, =0xFE02F000
    /* 清 .bss */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b
    bl      main
2:  b       2b
