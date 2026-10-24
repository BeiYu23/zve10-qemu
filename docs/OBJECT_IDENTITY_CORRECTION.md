Correction: the third decrement belongs to a different object type, so the pairing is one-to-one

My previous message said the counter is incremented once and decremented twice, the second decrement
coming from a destructor at 0x11B208. Checking object identity shows that is wrong.

The function at 0x11B208 reads two u16 at obj+0x04 and obj+0x06 and multiplies them at 0x11B228 to get an
element count, then indexes an array from obj+0x0C. A bitmap has a pointer at +0x04, the geometry
structure, so reading u16 there would yield pointer halves rather than dimensions. The object this
destructor operates on is therefore a different type, with u16 width and height at +0x04 and +0x06, an
array at +0x0C, and its own counter at +0x18.

So the bitmap counter is incremented at 0x11BCB2 by the write-lock and decremented at 0x11B5E4 by the
write-unlock, a clean one-to-one pair. The decrement at 0x11B250 touches a different class entirely.

The porting side concern still stands for the right reason: if the shim skips the increment while the real
unlock still runs, the single decrement underflows and reaches the assert. But the fix is simpler than I
described, since there is only one decrement to pair with.

The proposed shim remains the right shape: increment bitmap+0x18 in the interposed lock, decrement it in
the interposed unlock, and never touch bitmap+0x20, the field that governs whether to wait. With one
increment and one decrement, both interposed, the books balance.

The error was checking which functions write the field without checking whether they write it on the same
object. Enumerating writers is not enough; the object identity has to match too.
