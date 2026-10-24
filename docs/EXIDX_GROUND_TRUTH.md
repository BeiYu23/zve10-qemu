The ARM exception index settles function sizes and identifies the unnamed functions

The porting side is right that the dynsym sizes are accurate. Verifying against a source neither of us
had used, the .ARM.exidx table at 0x317668, gives the authoritative answer, since ARM EHABI delineates
each function by the start of the next index entry.

  GRM_gpermFillrect  0x1178A4, next entry 0x1179DE  -> 314 bytes, exactly matching dynsym
  GRM_gpermRectblit  0x1179DE, next entry 0x117D18  -> 826 bytes, against dynsym's 824

So the earlier claim that st_size was truncated is withdrawn. The prologue and epilogue do match as the
porting side showed, and dynsym agrees.

My own method, taking the next defined symbol as the end, was the one that overestimated, because it
cannot see functions that have no dynamic symbol. Both of us were half right and the table was the
tiebreaker.

The index also resolves where those two call sites live. Entries appear at 0x117D18, 0x117E34 and
0x117F3C, so the call sites at 0x117DF6 and 0x117F0A both fall inside an unnamed function starting at
0x117E34 and 264 bytes long. That is the unnamed static function the porting side suspected, now with an
exact address and size.

The parsing recipe is worth keeping: read eight-byte entries from .ARM.exidx, decode the first word as a
prel31 offset relative to the entry address to get the function start, then take the next entry's start
as the function end. This needs no symbol table at all and is compiler-generated, so it is a better
ground truth than prologue and epilogue matching, which can be confused by multiple exit points.
