/*
 * Freestanding libgcc glue, needed when the payload is linked with the GNU
 * arm toolchain (clang inlined these; GCC calls libgcc helpers).
 *
 * _dvmd_lnx.o (provider of __aeabi_idiv/__aeabi_idivmod) references raise()
 * from its divide-by-zero path. The payload never divides by zero; if it
 * ever did, the only sane freestanding behaviour is to park.
 */
int raise(int sig)
{
    (void)sig;
    for (;;)
        __asm__ volatile("wfi");
}
