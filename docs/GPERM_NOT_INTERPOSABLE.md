Critical: GRM_gpermFillrect cannot be interposed, because nothing imports it

Checked every shared library in both firmwares for an undefined reference to GRM_gperm. There are
none, in ZV-1F and in ZV-E10 alike. The two drawing functions are therefore called only from inside
libObj itself, through internal direct calls, not through the PLT.

That means symbol interposition of GRM_gpermFillrect or GRM_gpermRectblit will never fire. An
LD_PRELOAD shim defining those names would simply never be reached, because no module resolves them
externally.

The five GRM symbols appFw actually imports are GRM_fbrmSetScreenPreference,
GRM_fbrmCheckUpdateFinished, GRM_fbrmRegisterTriggerFrameworkCb, GRM_bitmapCreateFromPhys and
GRM_bitmapRelease. Drawing does not go through any of them. It goes through the imported ux::gfxeng
family, which internally reaches the gperm functions inside libObj.

Two viable approaches follow. Either patch libObj's own code at 0x1178A4 and 0x1179DE, which are
exported symbols that can be located by dlsym even though nothing imports them, replacing the entry
with a branch to the software implementation; or interpose at the ux::gfxeng layer the app does
import, which costs far more work because it means reimplementing the sprite engine.

Patching libObj is the practical route and matches how the other internals such as 0x8A2EE0 are
already being handled. Since all drawing funnels through these two internal functions, patching them
catches everything.

Verified alongside this: the format lookup table at 0x29D88E holds 2, 4, 2, 1, 2, 2, 2, 4, 4 for
formats 1 through 9, matching the porting side exactly, with the string BMP_Erro immediately after.

Also confirmed appFw imports only five GRM symbols in total, and that GRM_bitmapGetRowPixelCount is
not among them, so the dead-getter chain cannot be the cause of the QEMU crash.
