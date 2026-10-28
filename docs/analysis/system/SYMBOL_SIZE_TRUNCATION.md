Symbol sizes are truncated, which makes st_size useless for both gperm functions

The porting side is right that st_value is reliable while st_size is not, and that objdump's +0xNNN
labels are computed from symbol values rather than sizes. The check here goes further and shows the
truncation affects gpermRectblit too, not just the GraphicsImage methods.

Taking Thumb bits into account, the next dynamic function symbol after gpermRectblit at 0x1179DE is not
another named function until BMP_Create at 0x118F19. That makes the symbol-less region spanning
0x1179DE to 0x118F19 about 0x153B bytes, roughly 5435, while dynsym claims 824. So st_size is wrong by a
factor of six for this function, and the two call sites at 0x117DF6 and 0x117F0A are indeed inside that
region, exactly as objdump's annotations say. An earlier claim that they were outside came from trusting
st_size and was wrong.

For the specification this means the 314 and 824 byte figures recorded for gpermFillrect and
gpermRectblit should not be used for anything. The entry addresses 0x1178A4 and 0x1179DE are what matter
for interception, and both remain correct.

It also means that the region attributed to gpermRectblit very likely contains unnamed static helpers
rather than nothing, so the call sites inside it may belong to helpers rather than to gpermRectblit
itself. That does not change the interception plan, since all six calls go through the PLT and
interposing the two entry points catches them.

Practical rules worth keeping: use st_value and never st_size; beware that comparing a Thumb symbol value
against an address without clearing bit zero can make a symbol appear to follow itself.
