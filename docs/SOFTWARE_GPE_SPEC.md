Merged software-GPE specification for the QEMU line

Confirms the contract model: one 128KB mmap only, so no arena and no adjacent slicing. Both of
those are dead and were retracted.

Combines the porting side's gperm analysis with my bitmap analysis so the QEMU line does not
have to re-derive either half.

gpermFillrect takes four arguments and the third is a parameter structure. The destination bitmap
is at offset 0. Four colour bytes sit at 4 through 7, copied verbatim with no bit manipulation, so
channel order is still unconfirmed. Four int16 at 8, 0x0A, 0x0C and 0x0E are x0, y0, x1 and y1.
The mode at 0x10 accepts only 1 and 2 and the real firmware returns without drawing for anything
else, which a shim must copy exactly. Offset 0x14 is a flag or alpha byte.

gpermRectblit likewise takes four arguments, with the destination bitmap at 0 and the source
bitmap at 0x0C; its local structure is 156 bytes rather than 48, so it carries more parameters
that remain to be detailed.

For the destination, the bitmap is a C++ object of 0x80 bytes with a vtable at offset zero and a
calloc geometry structure at offset 4. Virtual slots are 0x14 for the sub-rectangle query, 0x18
for row pixel count which writes through an out pointer, 0x30 for physical address, 0x4C for
writeLockBits returning the writable pixel pointer, and 0x50 for writeUnlockBits. The geometry
structure starts with the magic 0x00ABCDEF and the sub-rectangle output is int16 x at 0, y at 2,
width at 4 and height at 6.

Critical risk: the object initialises a pthread mutex at 0x24 and a condition variable at 0x40,
which is GPU fence synchronisation. Calling the real writeLockBits in a shim with no GPU can block
indefinitely, so that lock and unlock pair should be interposed to return immediately.

screenGetOnBitmap returns zero in both firmwares, so it is a platform stub and must be left
returning zero. The app never uses it anyway: appFw imports GRM_bitmapCreate and
GRM_bitmapCreateFromPhys directly, so the app creates its own bitmaps. That makes GRM_bitmapCreate
the real source of the target buffer, and one of its roughly ten arguments carries the pixel
format. Logging those arguments is a faster and more certain route to the format and stride than
deducing them from observed pixel repetition, because no table maps a format value to a constant
anywhere in the create path.

CheckUpdateFinished returns zero only when the VDF status reads zero and 0xFFFFD8F1 otherwise, so
a shim should simply return zero. SetScreenPreference contains no pixel format: it maps
preferences 1 through 4 to codes 0xE800, 0xE802, 0xE803 and 0xE801 and routes them through the VDF
and resource port layers.

Two practical notes. Every GRM symbol address carries the Thumb bit, so disassembly needs addr & ~1.
And since the app creates its own bitmaps, one of the GRM_bitmapCreate calls should have a width and
height matching the screen, which is how the screen bitmap can be identified.
