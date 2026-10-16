/*
 * arch/arm/mach-cxd900x0/adrs_conv.c
 *
 * Copyright 2015 Sony Corporation
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  version 2 of the  License.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <mach/noncache.h>

#define BUG_SIMPLE	do { *(int *)0 = 0; for (;;); } while (0)

static int cxd900x0_adrs_debug;
module_param_named(debug, cxd900x0_adrs_debug, int, S_IRUSR|S_IWUSR);
static int cxd900x0_adrs_ignore_null;
module_param_named(null, cxd900x0_adrs_ignore_null, bool, S_IRUSR|S_IWUSR);

/* physical address */
#define ESRAM_BASE	CXD900X0_ESRAM_BASE
#define ESRAM_SIZE	CXD900X0_ESRAM_SIZE
#define ESRAM_END	(ESRAM_BASE + ESRAM_SIZE)

#define VA_ESRAM	ESRAM_BASE
#define VA_ESRAM_END	ESRAM_END

/* start address of uncached memory */
static unsigned long pa_uc[2] = { DDR0_END, DDR1_END };
static unsigned long va_uc[2] = { VA_DDR1,  VA_DDR1_END };

void __init parse_memrsv_hook(unsigned long start, unsigned long size,
			      unsigned int type)
{
	switch (type) {
	case MT_DEVICE:
	case MT_DEVICE_WC:
	case MT_DEVICE_SO:
	case MT_UNCACHED:
	case MT_MEMORY_NONCACHED:
	case MT_MEMORY_NC_XN:
		break;
	default:
		return;
	}
	if (DDR0_BASE <= start && start < DDR0_END) {
		if (start < pa_uc[0]) {
			pa_uc[0] = start;
			va_uc[0] = (ulong)__va(start);
		}
	} else if (DDR1_BASE <= start && start < DDR1_END) {
		if (start < pa_uc[1]) {
			pa_uc[1] = start;
			va_uc[1] = (ulong)__va(start);
		}
	}
}

static inline int pass_through(unsigned long x)
{
	return (!x  &&  cxd900x0_adrs_ignore_null);
}

static inline void show_info(void)
{
	switch (cxd900x0_adrs_debug) {
	case 1:
		__backtrace();
		break;
	case 2:
		__backtrace();
		BUG_SIMPLE;
		break;
	default:
		break;
	}
}

unsigned long arch_va_to_phys(unsigned long va)
{
	if (pass_through(va))
		return va ? va : VA_DDR0;
	if (likely(va < VA_DDR1)) {
		if (likely(va >= VA_DDR0))
			return va - VA_DDR0;
	} else if (likely(va < VA_DDR1_END)) {
		return va - VA_DDR1 + DDR1_BASE;
	} else if (likely(VA_ESRAM <= va  &&  va < VA_ESRAM_END)) {
		return va;
	}
	printk(KERN_ERR "ERROR:%s:va=0x%08lx\n", __func__, va);
	show_info();
	return PA2NC_ERR;
}

unsigned long arch_va_to_noncache(unsigned long va)
{
	if (pass_through(va))
		return va ? va : VA_DDR0;
	if (likely(va_uc[0] <= va && va < VA_DDR1)) {
		return va;
	} else if (likely(va_uc[1] <= va && va < VA_DDR1_END)) {
		return va;
	} else if (likely(VA_ESRAM <= va && va < VA_ESRAM_END)) {
		return va;
	}
	printk(KERN_ERR "ERROR:%s:va=0x%08lx\n", __func__, va);
	show_info();
	return PA2NC_ERR;
}

unsigned long arch_va_to_cache(unsigned long va)
{
	if (pass_through(va))
		return va ? va : VA_DDR0;
	if (likely(VA_DDR0 <= va && va < va_uc[0])) {
		return va;
	} else if (likely(VA_DDR1 <= va && va < va_uc[1])) {
		return va;
	}
	printk(KERN_ERR "ERROR:%s:va=0x%08lx\n", __func__, va);
	show_info();
	return PA2NC_ERR;
}

unsigned long arch_phys_to_va(unsigned long pa)
{
	if (pass_through(pa))
		return pa ? pa : VA_DDR0;   /* QEMU: 0 会害调用方解引用 NULL */
	if (likely(pa < DDR0_END)) {
		return pa + VA_DDR0;
	} else if (likely(DDR1_BASE <= pa && pa < DDR1_END)) {
		return pa - DDR1_BASE + VA_DDR1;
	} else if (ESRAM_BASE <= pa && pa < ESRAM_END) {
		return pa;
	}
	printk(KERN_ERR "ERROR:%s:pa=0x%08lx\n", __func__, pa);
	show_info();
	/* QEMU: 兜底为已映射的 DDR0 虚拟地址，避免调用方解引用 NULL。
	 * 真机上（数据正确时）这些分支不会走到。 */
	return VA_DDR0;
}

unsigned long arch_phys_to_noncache(unsigned long pa)
{
	if (pass_through(pa))
		return pa ? pa : VA_DDR0;   /* QEMU: 0 会害调用方解引用 NULL */
	if (likely(pa_uc[0] <= pa && pa < DDR0_END)) {
		return pa + VA_DDR0;
	} else if (likely(pa_uc[1] <= pa && pa < DDR1_END)) {
		return pa - DDR1_BASE + VA_DDR1;
	} else if (ESRAM_BASE <= pa && pa < ESRAM_END) {
		return pa;
	}
	printk(KERN_ERR "ERROR:%s:pa=0x%08lx\n", __func__, pa);
	show_info();
	/* QEMU: 兜底为已映射的 DDR0 虚拟地址，避免调用方解引用 NULL。
	 * 真机上（数据正确时）这些分支不会走到。 */
	return pa + VA_DDR0;   /* QEMU: 正确换算（同 phys_to_cache） */
}

unsigned long arch_phys_to_cache(unsigned long pa)
{
	if (pass_through(pa))
		return pa ? pa : VA_DDR0;   /* QEMU: 0 会害调用方解引用 NULL */
	if (likely(DDR0_BASE <= pa && pa < pa_uc[0])) {
		return pa + VA_DDR0;
	} else if (likely(DDR1_BASE <= pa && pa < pa_uc[1])) {
		return pa - DDR1_BASE + VA_DDR1;
	}
	printk(KERN_ERR "ERROR:%s:pa=0x%08lx -> 0x%08lx (pa_uc0=0x%08lx)\n",
	       __func__, pa, pa + VA_DDR0, (unsigned long)pa_uc[0]);
	show_info();
	/* QEMU: 兜底做**正确换算**（pa + VA_DDR0），而不是一律返回 DDR0 基址。
	 * 之前一律返回 VA_DDR0 会让调用方（如 backup.ko 取预填数据）拿到
	 * 0x60000000 这种错误地址，导致其内部表指针为 NULL。 */
	return pa + VA_DDR0;
}

int arch_phys_to_memtype(unsigned long pa)
{
	if (likely(pa < DDR0_END)) {
		return PA2NC_DDRA;
	} else if (likely(DDR1_BASE <= pa && pa < DDR1_END)) {
		return PA2NC_DDRB;
	} else if (ESRAM_BASE <= pa && pa < ESRAM_END) {
		return PA2NC_ESRAM;
	}
	return PA2NC_ERR;
}

int arch_va_to_memtype(unsigned long va)
{
	if (likely(va < VA_DDR1)) {
		if (likely(va >= VA_DDR0))
			return PA2NC_DDRA;
	} else if (likely(va < VA_DDR1_END)) {
		return PA2NC_DDRB;
	} else if (likely(VA_ESRAM <= va  &&  va < VA_ESRAM_END)) {
		return PA2NC_ESRAM;
	}
	return PA2NC_ERR;
}

int arch_phys_to_cachetype(unsigned long pa)
{
	if (likely(DDR0_BASE <= pa  &&  pa < DDR0_END)) {
		return (pa < pa_uc[0]) ? PA2NC_CA : PA2NC_UC;
	} else if (likely(DDR1_BASE <= pa  &&  pa < DDR1_END)) {
		return (pa < pa_uc[1]) ? PA2NC_CA : PA2NC_UC;
	} else if (likely(ESRAM_BASE <= pa  &&  pa < ESRAM_END)) {
		return PA2NC_UC;
	}
	return PA2NC_ERR;
}

int arch_va_to_cachetype(unsigned long va)
{
	if (likely(VA_DDR0 <= va  &&  va < VA_DDR1)) {
		return (va < va_uc[0]) ? PA2NC_CA : PA2NC_UC;
	} else if (likely(VA_DDR1 <= va  &&  va < VA_DDR1_END)) {
		return (va < va_uc[1]) ? PA2NC_CA : PA2NC_UC;
	} else if (likely(VA_ESRAM <= va  &&  va < VA_ESRAM_END)) {
		return PA2NC_UC;
	}
	return PA2NC_ERR;
}

int arch_phys_to_cachetype2(unsigned long pa, unsigned long size)
{
	unsigned long pe = pa + size - 1;

	if (likely(DDR0_BASE <= pa  &&  pa < DDR0_END)) {
		if (pe < pa_uc[0])
			return PA2NC_CA;
		if (pa_uc[0] <= pa  &&  pe < DDR0_END)
			return PA2NC_UC;
		return PA2NC_MIX;
	} else if (likely(DDR1_BASE <= pa  &&  pa < DDR1_END)) {
		if (pe < pa_uc[1])
			return PA2NC_CA;
		if (pa_uc[1] <= pa  && pe < DDR1_END)
			return PA2NC_UC;
		return PA2NC_MIX;
	} else if (likely(DDR1_BASE <= pe  &&  pe < DDR1_END)) {
		return PA2NC_MIX;
	} else if (likely(ESRAM_BASE <= pa  &&  pa < ESRAM_END)) {
		return PA2NC_UC;
	}
	return PA2NC_ERR;
}

EXPORT_SYMBOL(arch_va_to_phys);
EXPORT_SYMBOL(arch_va_to_noncache);
EXPORT_SYMBOL(arch_va_to_cache);
EXPORT_SYMBOL(arch_phys_to_va);
EXPORT_SYMBOL(arch_phys_to_noncache);
EXPORT_SYMBOL(arch_phys_to_cache);
EXPORT_SYMBOL(arch_phys_to_memtype);
EXPORT_SYMBOL(arch_va_to_memtype);
EXPORT_SYMBOL(arch_phys_to_cachetype);
EXPORT_SYMBOL(arch_va_to_cachetype);
EXPORT_SYMBOL(arch_phys_to_cachetype2);
