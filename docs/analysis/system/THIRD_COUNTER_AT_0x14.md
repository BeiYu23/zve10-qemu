The release slot does not touch the known counters, but it decrements a third one at 0x14

Resolving the open caveat about vtable slot 0x58, which is 0x11BE18 and 96 bytes long ending at 0x11BE78.
It never touches bitmap+0x18, bitmap+0x1C or bitmap+0x20, so the porting side concern is resolved and it
cannot disturb the lock and unlock pairing.

It does however contain the same decrement-and-assert pattern on a different field. At 0x11BE28 it loads
bitmap+0x14, subtracts one, stores it back, and branches to an assert when the result is negative, after
taking the mutex.

So the bitmap carries at least four counter-like fields: 0x14, 0x18, 0x1C and 0x20. Our interposed lock
and unlock touch only 0x18, so this path stays balanced and needs no handling for the shim to work.

It is worth adding bitmap+0x14 to the periodic self-check alongside 0x18 and 0x1C, because release
decrements it and the assert fires on underflow. If the app lifecycle ever over-releases, the symptom
would be an assert rather than a visual fault, and having that field logged makes the difference obvious.

Also confirming the exidx boundary by an independent method: the function at 0x108C84 begins with stmdb
saving r4 through r9 plus sl, fp and lr, nine registers, while the next entry at 0x108E4C begins with a
different prologue saving eight registers without fp. A different register set at the claimed boundary
corroborates that a new function really starts there, which is an independent check rather than re-running
the same derivation.
