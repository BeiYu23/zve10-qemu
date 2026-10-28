An exidx lookup tool, and a correction to a claim we all shared

My earlier statement that both call sites fall in the 264-byte function at 0x117E34 was wrong, as the
porting side pointed out. The bounds are 0x117D18 to 0x117E34 for the 284-byte function containing
0x117DF6, and 0x117E34 to 0x117F3C for the 264-byte function containing 0x117F0A. Two distinct unnamed
functions, not one.

To stop making this class of error, here is a reusable lookup: parse .ARM.exidx as eight-byte entries,
decode each first word as a prel31 offset from the entry address to get the function start, sort the
starts, and a given address belongs to the last start at or below it, with the end being the next start.
It needs no symbol table.

Running it over the addresses in question corrects a claim all three of us shared. The call to
gpermRectblit at 0x108E1E is not inside GraphicsImage::clear. The exidx entries show clear at 0x108C52
spanning only sixteen bytes to 0x108C62, and 0x108E1E belongs to a separate unnamed function at 0x108C84
that runs 456 bytes to 0x108E4C.

That gives a more accurate call graph. clear is a sixteen-byte wrapper that sets two arguments to one and
tail-calls 0x108BFC, an 86-byte function that reads the internal object, calls GetSubRectangle, and
branches to 0x108B90, 108 bytes, which reaches 0x112550, 156 bytes, where it calls gpermFillrect. The
gpermRectblit call at 0x108E1E comes from the separate 456-byte function at 0x108C84 instead.

So clear reaches gpermFillrect, and gpermRectblit is reached from a different entry. Interception is
unaffected since both remain PLT calls to the two entry points, but the path description is now accurate.
