Interposition is technically viable, so the zero log count is an implementation issue

Checked the three conditions that would block LD_PRELOAD interposition of libObj's own exports, and all
three are clear.

There is no FLAGS or FLAGS_1 entry in libObj's dynamic section, so it is not linked with -Bsymbolic and
its self-referential PLT entries resolve through the global symbol search order rather than binding
internally. There is no VERDEF at all, so there is no symbol versioning to match. And both
GRM_gpermFillrect and GRM_gpermRectblit are GLOBAL DEFAULT FUNC symbols with unmangled C names, at 314
and 824 bytes.

So the prerequisites hold and interposition should fire. The zero log count therefore points at the shim
itself. The likely causes, in order of probability, are a definition compiled with C++ linkage so the
symbol is mangled and never matches, a symbol hidden by visibility settings, a shim that is not actually
loaded in the process that draws, or a different process than the one carrying the preload.

The diagnostic order worth following: nm -D on the shim to confirm the exported name is exactly
GRM_gpermFillrect with no mangling, then confirm the shim appears in the target process's maps, then
confirm the shim's constructor runs at all by logging from it, and only then test the interposed call.
Using extern C on the definition is the single most likely fix.
