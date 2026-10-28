The null dereference may be the missing pixel pointer, which reorders the checklist

Accepting the correction that zero log lines more likely means the process dies before reaching the
drawing code, so the shim diagnostics were aimed at the wrong layer.

There is a concrete hypothesis that explains all three observations at once: address zero, death before
any successful drawing, and no SWGPE lines. It is the pixel pointer being null.

The chain is that FillRectParam+0x00 gives the bitmap, and the actual pixel writes use
[bitmap+0x08], which was established earlier to be zeroed by the constructor and not set by the
initialiser, only becoming valid once something maps the physical buffer the app supplies through
GRM_bitmapCreateFromPhys. If that mapping never happens, [bitmap+0x08] stays null and the first pixel
write faults at address zero, before any log line from an interposed drawing call could appear.

So item four in the spec, establishing the mapping in GRM_bitmapCreateFromPhys, is not a later step; it
has to precede the gperm interception, otherwise the first draw dies on a null pixel base.

The predicting check is cheap: interpose GRM_bitmapCreateFromPhys, log the physical address it receives,
and after the call log [bitmap+0x08]. If the second value is zero while the first is not, the hypothesis
is confirmed and the mapping step is the fix.
