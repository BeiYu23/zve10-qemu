The counter model needs one caveat: a second counter and a second increment path exist

The step-by-step confirmation of the wait mechanism is correct: 0x11BAF4 returns zero when bitmap+0x20 is
non-zero, and otherwise returns one only when bitmap+0x18 is zero, and the caller skips
pthread_cond_wait only when that return is non-zero.

But the inference that the first fill always passes depended on an assumption that is now shown to be
incomplete, namely that the two counters start at zero and only bitmap+0x20 changes.

Writes to bitmap+0x18 do exist in the bitmap code. Two sites decrement it, at 0x11B250 and 0x11B5E4,
each taking a lock first. A third site at 0x11BCB2 increments it, and immediately afterwards loads the
pixel pointer from bitmap+0x08 and stores it through the out pointer, which makes that a separate
write-lock variant for an argument value other than zero or minus one.

So the accurate statement is not that the first fill passes and the second hangs. It is that a minus-one
call skips the wait only when both counters happen to be zero at that moment, and either counter being
non-zero from prior activity makes it block on the first call.

This does not weaken the mandatory conclusion. It strengthens it, because the blocking condition has two
independent causes and the shim cannot rely on any particular counter state. The shim should return the
pixel pointer immediately without entering the wait at all, regardless of the counters.

Practical consequence: do not predict a specific crash-then-hang sequence when reading test results.
Observe whether it blocks or faults and treat either as consistent with a counter being non-zero and a
null pixel pointer respectively.
