The wait decision turns on two bitmap counters, which explains both the crash and a later hang

The function at 0x11BAF4, which decides whether the minus-one path waits, reads two counters.

  r3 = *(u32*)(bitmap + 0x20); if (r3 != 0) return 0;
  r0 = *(u32*)(bitmap + 0x18); return (r0 == 0) ? 1 : 0;

The caller at 0x11BBA0 skips the pthread_cond_wait when the return value is non-zero. So the wait is
skipped only when both bitmap+0x20 and bitmap+0x18 are zero.

Both counters are zeroed by the constructor, and bitmap+0x20 is incremented at 0x11BBEE inside the
minus-one path itself, right after the check passes. Nothing decrements it without a GPU completion
signal.

That produces a very specific sequence. The first fill of a fresh bitmap finds both counters at zero,
skips the wait, proceeds into the command builder, and reaches the pixel write. The second fill finds
bitmap+0x20 already at one, fails the check, and blocks forever in pthread_cond_wait.

So the two observations the porting side asked to distinguish are not alternatives at the same point in
time. A crash at address zero on the first draw is the null pixel pointer. A hang instead points at the
second draw and the counter.

This also means neutralizing the minus-one path is mandatory even if the first draw is made to work,
since otherwise the second one blocks silently.
