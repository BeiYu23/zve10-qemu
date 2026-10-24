I was wrong: the real fill path does block, and this makes interposing WriteLockBits mandatory

The porting side was right that my scan missed an indirect call. Following it changes the conclusion.

At 0x118B9C inside the fill routine, the code sets r1 to 0xFFFFFFFF and r2 to zero, then calls
GRM_bitmapWriteLockBits through the PLT at 0x118BA2. Tracing that argument through the
implementation at 0x11BB68 shows it takes the blocking branch: the comparison at 0x11BB94 matches
0xFFFFFFFF, the helper at 0x11BAF4 is called, and then execution reaches the pthread_cond_wait at
0x11BBA8 on the condition variable at bitmap+0x40.

So the real gpermFillrect is synchronous. It waits inside WriteLockBits for the GPU to signal
completion. My earlier claim that it is asynchronous was wrong, and it was wrong because the grep I
used filtered for blocking primitives appearing literally inside the two functions, which cannot see
a blocking primitive reached through a PLT call. The question I answered was narrower than the one I
asked.

The consequences flip. The porting side original reasoning was correct: replacing gpermFillrect does
remove a real wait. And more importantly, if the shim replaces gpermFillrect but any other code path
still calls GRM_bitmapWriteLockBits with minus one, that path will hang forever with no output.
Interposing GRM_bitmapWriteLockBits and BMP_WriteLockBits to neutralize the blocking branch is
therefore required rather than optional.

CheckUpdateFinished remains necessary, but as a separate mechanism: it polls VDF status rather than
waiting on the bitmap condition variable.
