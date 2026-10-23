# Darwin: the power-supervisor MCU behind the 5-9 minute session limit

## What it is

The camera has a second processor — "Darwin", the system power MCU — that the
AP talks to over **SIO channel 0 at `0xF1005000`**. The link carries 0x80-byte
packets with a CRC-16/CCITT-FALSE, and a GPIO line acts as chip select.

The 7M4 payload (`ILCE-7M4-RE`, `payloads/fastboot/src/darwin.c`) documents
why this matters:

> The Virtual WDT is why this exists: Darwin counts four one-byte
> down-counters at 0x200026FC and **kills the system when one reaches zero.
> Nothing in bootrom context re-arms them.**

A reload byte of `0x0A` means the AP populated Darwin's config during a normal
boot this power cycle; `0x00` means it did not. A counter of `0xFF` is the
disabled sentinel; a live tick turns `0x00` into `0xFF` within one 100 ms
period.

## Why this explains our observations exactly

| Observation | Explanation |
|---|---|
| Sessions die after 5-9 min, whether idle or busy | Darwin's counter reaches zero on its own clock |
| A 2 s USB `getvar` heartbeat did not help | Darwin watches the **SIO link**, not USB |
| The AP-side TIMER-IP watchdog registers read 0 yet the session still died | That watchdog is a different mechanism, inside the AP |
| `cxd900x0_poweroff()` is a stub | Power authority is not in the AP at all |
| Sessions that worked continuously still died mid-run | Work does not feed Darwin; only SIO traffic does |

## Confirmed on ZV-E10 hardware/firmware

- `0xF1005000` (SIO base) appears **12 times** in `nflasha15` (`/usr`) and
  twice in `nflasha3` (`/system`)
- "Darwin" appears **71 times** (plus 29 lowercase "darwin") in `nflasha15`
- the SoC is the same family: 7M4's kernel defines
  `SIO_IO_BASE(ch) = 0xF1005000 + SIO_IO_SIZE * ch`
  (`drivers/udif/mach-cxd900xx/platform.c:217`)

## Register layout (CXD90045 vs CXD90057)

| Item | 7M4 (CXD90057) | ZV-E10 (CXD90045) |
|---|---|---|
| SIO base | `0xF1005000 + 0x1000*ch` | **`0xF1005000 + 0x1000*ch`** (same) |
| GPIO base | `0xF101D000` | **`0xF2910000`** |
| GPIO stride | `0x1000` per port | **`0x100` per port** (`ch << 8`) |
| DIR set / WDATA set / WDATA clr | `+0x04` / `+0x44` / `+0x48` | **same** (validated: the LED driver uses `0x44`/`0x48`) |
| Darwin chip select | GPIO port 4, bit 2 | **port/bit not yet identified** |

So the packet format and the register offsets carry over; only the GPIO base,
the port stride and the XCS pin number differ — and the pin number is the one
unknown left.

## Next hardware session: three cheap probes (no code needed)

`oem peek` on the running payload answers everything, because the BootROM
must already have brought Darwin up to boot the AP at all:

1. **Is SIO initialised?** `oem peek:0xf1005000:0x20`
   Expect a plausible CTRL/CFG/RATE setup if the ROM left the link configured.
2. **Which GPIO port carries the chip select?** walk the ports:
   `oem peek:0xf2910400:0x50` (port 4), then ports 0-7 similarly; look for a
   line that is driven (DIR bit set) and idle high — XCS is active low.
3. **Can a read reach Darwin?** once 1 and 2 look right, send the 7M4 command
   0x16 packet for `0x200026FC` (the VWDT counter table) and check whether a
   valid CRC comes back. Reads are not bounds-checked and change nothing.

## Then the actual fix (matches the field advice)

Send a harmless SIO exchange periodically — "reading counts as feeding":

- `oem darwin` in the payload reports the four counters and three reload bytes
  (command 0x16 read at `0x200026FC` / `0x2000003C`), copying 7M4's
  `darwin_vwdt_state()`
- a periodic keepalive from the payload's main loop (every few seconds) keeps
  the link alive without host involvement, so **long dumps and flashes stop
  needing session batching**
- the host tools additionally call `oem darwin` before each push/pull, as a
  belt-and-braces measure

If reading turns out not to feed the counter (the 7M4 comment calls the read
"without touching it"), the fallback is the documented write: Darwin's config
blocks are 0x78 bytes and the VWDT table sits `0x14` bytes into block 83
(`83 * 0x78 = 0x26E8`), so command 0x12 — an idempotent, explicitly supported
write — can re-arm or disarm a channel.

## Status

- protocol transcribed and understood: yes (from ILCE-7M4-RE)
- ZV-E10 SIO/Darwin present: yes (firmware evidence above)
- XCS pin identified: **no** — needs the probe above or AP-loader disassembly
- ported to the payload: **not yet** (waiting on the pin)
