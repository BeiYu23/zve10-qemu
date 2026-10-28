Retraction: the gperm functions are fully interceptable, and there are no direct calls at all

My earlier conclusion that GRM_gpermFillrect could not be interposed was wrong. The porting side
challenged it with JUMP_SLOT relocations, and checking confirms they are correct.

Both firmwares carry R_ARM_JUMP_SLOT relocations for these symbols. In ZV-1F libObj the slots are at
0x35473C for GRM_gpermRectblit and 0x354A04 for GRM_gpermFillrect; in ZV-E10 libObj they are at
0x13D7D5C and 0x13D92DC. There are 885 JUMP_SLOT relocations in ZV-1F libObj, so calling its own
exports through its own PLT is completely normal for this library.

The reasoning error was treating no external import as equivalent to no PLT indirection. A library
resolving its own exported symbol through its PLT still creates a JUMP_SLOT, and the dynamic linker
resolves that symbol through the global search order, which is exactly what LD_PRELOAD sits in front
of. So interposition works.

Scanning the whole text section for BL and B.W instructions that target the function bodies at
0x1178A4 and 0x1179DE found none at all, for either function. Every call goes through the PLT, which
means interposition covers one hundred percent of call sites. That makes the case stronger than the
porting side argued, not merely equal.

So the recommendation flips: try interposition first with a single log line to confirm the hook fires,
and keep binary patching as a fallback only. That avoids the risk of rewriting entry instructions,
dealing with Thumb branch stubs and flushing caches for no reason.
