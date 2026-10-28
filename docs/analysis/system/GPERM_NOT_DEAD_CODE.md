gperm is not dead code: six real PLT calls exist

The claim that GRM_gperm is completely uncalled does not hold. Counting only genuine PLT calls with
objdump, rather than a hand-written decoder, gives six:

  0x108E1E  blx 3a7e0 <GRM_gpermRectblit@plt>
  0x1125D0  blx 3b0ac <GRM_gpermFillrect@plt>
  0x117DF6  blx 3a7e0 <GRM_gpermRectblit@plt>
  0x117F0A  blx 3a7e0 <GRM_gpermRectblit@plt>
  0x126B6E  blx 3a7e0 <GRM_gpermRectblit@plt>
  0x22A134  blx 3a7e0 <GRM_gpermRectblit@plt>

That is one call to fillrect and five to rectblit. All six are blx to the PLT entries at 0x3B0AC and
0x3A7E0, and there are no direct branches to the function bodies, so the earlier conclusion that every
call goes through the PLT is confirmed rather than refuted.

The call at 0x108E1E sits in the neighbourhood of ux::core::GraphicsImage::clear, which is a genuine
rendering entry point, and 0x117DF6 and 0x117F0A sit inside gpermRectblit itself, which uses fillrect
internally.

So the software GPE approach at the gperm layer remains valid and should not be abandoned. The zero
result came from the same hand-written BL/BLX decoder that previously mis-decoded the PLT addresses,
so its output cannot be trusted for either conclusion.

The reproducible check is a single objdump grep for blx against the two PLT entry addresses.
