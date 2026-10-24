GraphicsImage is a good target, and the chain to the bitmap is now verified end to end

Two things at once: correcting the dead-code claim again, and confirming that the pivot to
GraphicsImage is a better choice.

GRM_gperm is still not dead code. Counting genuine PLT calls with objdump gives one blx to
GRM_gpermFillrect@plt at 0x1125D0 and five to GRM_gpermRectblit@plt at 0x108E1E, 0x117DF6, 0x117F0A,
0x126B6E and 0x22A134. The reproducible command is an objdump grep for blx against the two PLT entries
at 0x3B0AC and 0x3A7E0.

That said, GraphicsImage is the better interception point, and the analysis explains why: the call at
0x108E1E sits in the anonymous region between GraphicsImage::clear at 0x108C53 and the next symbol at
0x108E4D, so gpermRectblit is reached from inside the clear path.

Tracing clear's real body at 0x108BFC gives the whole chain. It reads the internal object from
image+0x18, calls 0x1192B4 which is the GetSubRectangle implementation, reads four u16 at offsets 0, 2,
4 and 6 of the returned structure as x, y, width and height, then calls 0x108B90 with the rectangle and
the colour.

Two useful confirmations follow. sys_getInternalObject at 0x109241 simply returns image+0x18, so the
internal object is at that offset. And the rectangle layout of x, y, width, height as u16 at 0, 2, 4
and 6 is now confirmed from a second independent call site, since the porting side first saw it in
gpermFillrect.

On interposition: clear has no JUMP_SLOT while copy and sys_getInternalObject do. So internal calls to
clear are direct and would not be intercepted, but appFw imports it as an undefined symbol and reaches
it through its own PLT, which is exactly the path worth intercepting.
