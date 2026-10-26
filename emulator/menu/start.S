/* ARM EABI startup, original loader passes rtld_fini in r0. */
.syntax unified
.arm
.global _start
.type _start,%function
_start:
    mov fp, #0
    mov lr, #0
    ldr r1, [sp], #4
    mov r2, sp
    push {r2}          /* stack_end */
    push {r0}          /* rtld_fini */
    mov r3, #0
    push {r3}          /* fini */
    ldr r0, =main
    bl __libc_start_main
    mov r7, #1
    mov r0, #127
    svc #0
.size _start, .-_start
.section .note.GNU-stack,"",%progbits
