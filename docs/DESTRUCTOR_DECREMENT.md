The third decrement site is a destructor, which makes the counting one-to-many

Resolving what the third decrement site belongs to. The function at 0x11B208, 164 bytes, first loads the
array pointer from obj+0x0C, then loops over obj+0x04 times obj+0x06, which are the width and height read
as u16, calling vtable offset 0x58 on each element, then frees the array with free, and finally reaches
the decrement of bitmap+0x18 at 0x11B250.

So it is a destructor or cleanup routine, and it decrements the same counter that writeLockBits
increments at 0x11BCB2 and that writeUnlockBits decrements at 0x11B5E4.

That gives one increment against two decrements, from three different functions, which is why a shim that
merely skips the lock side will unbalance whichever of the two decrements still runs.

A design that keeps the books balanced without touching the blocking counter is to have the shim maintain
bitmap+0x18 explicitly: increment it in the interposed lock and decrement it in the interposed unlock,
while never touching bitmap+0x20, which is the field that decides whether to wait. Both sides interposed,
the pair balances, and nothing reaches the condition variable.

There is one open risk to check before adopting that design. The constructor stores zero at bitmap+0x18,
and the destructor decrements it, so a destructor that runs on an object whose count is zero would
underflow and hit the assert at line 426. Whether that happens depends on what the release path at
0x119524 does before reaching slot 0x58, which has not been traced, so the destructor path needs checking
before the shim is finalised.
