The unlock side must be neutralised too, and the exact functions are now identified

The porting side is right that returning the pixel pointer without incrementing the reference count will
underflow it later, since the decrement sites assert when the value goes negative. Using the exidx lookup
to place each counter operation in its owning function gives precise targets.

  0x11BCB2  [+0x18]++   in 0x11BB68 (396 bytes)  the write-lock implementation
  0x11B250  [+0x18]--   in 0x11B208 (164 bytes)
  0x11B5E4  [+0x18]--   in 0x11B584 (152 bytes)
  0x11BB90  [+0x1C]++   also in 0x11BB68
  0x11BBE6  [+0x1C]--   also in 0x11BB68

All three functions are unnamed in dynsym, but 0x11BB68 is the write-lock implementation analysed
earlier and 0x11B584 is the vtable slot 0x50 target the porting side identified as writeUnlockBits, which
the decrement site at 0x11B5E4 confirms. The third, 0x11B208, is another decrementing path, most likely
release.

So the increment lives in write-lock and the decrements live in two separate functions, which is exactly
the asymmetry that makes neutralising only the lock side unsafe. Both the unlock path and the release path
have to avoid decrementing, which in practice means interposing GRM_bitmapWriteUnlockBits as a no-op and
checking whether release also touches the counter.

The general lesson worth recording: for any paired interface, find the writers on both sides before
neutralising one. The counter is incremented in one function and decremented in two, so the pairing is
one-to-many and a symmetric shim is not enough.
