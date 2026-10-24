Coverage audit: the GPU command builder has exactly one caller, and it is gpermFillrect

Two checks, both bearing on whether intercepting the gperm layer covers everything.

First, the function at 0x1156C0, which builds the GPU command object with new(0x78), has exactly one
caller in the whole text section: the bl at 0x1179CC. That address lies inside gpermFillrect, which
spans 0x1178A4 to 0x1179DE. So every GPU command submission in libObj funnels through gpermFillrect.

Second, the porting side chain through the clear path checks out: 0x108B90 does call 0x112550, at
0x108BEA, which in turn reaches gpermFillrect at 0x1125D0.

Together these mean intercepting gpermFillrect and gpermRectblit covers the drawing path completely,
including both entries from GraphicsImage::clear, and there is no separate command submission route
bypassing them.

One caution on symbol extents: the nearest-symbol annotation objdump prints is not a reliable statement
of which function an address belongs to, since some dynamic symbol sizes are truncated to 16 bytes while
the actual bodies run far longer. Membership should be checked by comparing against the byte range from
the symbol value to the next symbol, as was done here for 0x1179CC inside gpermFillrect.
