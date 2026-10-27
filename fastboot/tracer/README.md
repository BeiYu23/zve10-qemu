# Boot tracer

`tracer.c` installs as `/usr/bin/bootin.elf`, records that init reached the
exec (and with which argv), then `execve`s `/usr/bin/bootin.real`.

It exists because a failed boot leaves no readable evidence: init logs only to
the console, `/usr` mounts read-only, and `/tmp` is cleared on every boot. The
marker is written to `/setting/zvegisk/trace`, which survives resets and is
readable over fastboot.

Interpretation:

| trace content | meaning |
|---|---|
| `bootin-reached argv=...` | init mounted /usr, loaded the kmods and reached the exec |
| `bootin.execve-FAILED` | /usr mounted but bootin.real would not start |
| no file | /usr did not mount, or init failed earlier |
