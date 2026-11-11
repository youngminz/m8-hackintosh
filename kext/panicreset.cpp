//
// panicreset.cpp: restart after a kernel panic through the FCH reset register (CF9).
//
// The M8's FADT reset register is an SMI (port 0xB2 <- 0xBE). On 2026-11-11 a Tahoe panic with the
// GPU and VCN busy (fq_codel assert) saved its panic log and then never restarted: the box stayed
// hung until the power button. A normal restart works, but it runs our psp-exit teardown first.
// For the panic restart types this does what PEHaltRestartInternal does for them (platform panic
// actions), then a full reset through CF9 (cold: PLTRST plus power cycle), with the keyboard
// controller as a fallback; only if both return does the stock path run.
//
//   sysctl debug.rmb_panicreset   1 (default) CF9 full reset on panic, 0 stock path
//   sysctl -w debug.rmb_panic=0x7061   test panic
//   -rmbnopanicreset              don't route at all
//

#include "rmb.hpp"
#include <sys/sysctl.h>

namespace {

mach_vm_address_t orgHaltRestartInternal;
void (*gRunPanicActions)(uint32_t, uint32_t);
int gPanicReset = 1;

SYSCTL_INT(_debug, OID_AUTO, rmb_panicreset, CTLFLAG_RW | CTLFLAG_KERN, &gPanicReset, 0,
           "RembrandtGfx: 1 = restart after a panic through CF9 full reset, 0 = stock");

int panicSysctl(struct sysctl_oid *, void *, int, struct sysctl_req *req) {
	int v = 0;
	int err = SYSCTL_OUT(req, &v, sizeof(v));
	if (err || !req->newptr)
		return err;
	if ((err = SYSCTL_IN(req, &v, sizeof(v))))
		return err;
	if (v != 0x7061)
		return EINVAL;
	panic("RembrandtGfx: test panic (debug.rmb_panic, panicreset %d)", gPanicReset);
	return 0;
}

SYSCTL_PROC(_debug, OID_AUTO, rmb_panic, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_KERN, nullptr, 0, panicSysctl, "I",
            "RembrandtGfx: write 0x7061 to panic (tests the panic restart)");

inline void portOut(uint16_t port, uint8_t v) { asm volatile("outb %0, %1" : : "a"(v), "Nd"(port)); }
inline uint8_t portIn(uint16_t port) {
	uint8_t v;
	asm volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}

int wrapHaltRestartInternal(unsigned type, uint32_t details) {
	// kPEPanicRestartCPU (4), kPEPanicRestartCPUNoCallouts (9: no platform actions)
	if ((type == 4 || type == 9) && gPanicReset) {
		if (type == 4 && gRunPanicActions)
			gRunPanicActions(4, details);
		uint8_t cf9 = portIn(0xCF9) & ~0x0E;
		portOut(0xCF9, cf9 | 0x02);  // SYS_RST: arm
		IODelay(50);
		portOut(0xCF9, cf9 | 0x0E);  // RST_CPU + FULL_RST: cold reset
		IODelay(500000);
		portOut(0x64, 0xFE);  // keyboard controller pulse reset
		IODelay(500000);
	}
	return FunctionCast(wrapHaltRestartInternal, orgHaltRestartInternal)(type, details);
}

}  // namespace

void panicResetStart(KernelPatcher &patcher) {
	sysctl_register_oid(&sysctl__debug_rmb_panic);
	if (checkKernelArgument("-rmbnopanicreset")) {
		LOG("panicreset: off (-rmbnopanicreset)");
		return;
	}
	gRunPanicActions = reinterpret_cast<decltype(gRunPanicActions)>(
	    patcher.solveSymbol(KernelPatcher::KernelID, "_IOCPURunPlatformPanicActions"));
	patcher.clearError();
	KernelPatcher::RouteRequest req {"_PEHaltRestartInternal", wrapHaltRestartInternal, orgHaltRestartInternal};
	if (!patcher.routeMultiple(KernelPatcher::KernelID, &req, 1)) {
		LOG("panicreset: route PEHaltRestartInternal failed (err %d)", patcher.getError());
		patcher.clearError();
		return;
	}
	sysctl_register_oid(&sysctl__debug_rmb_panicreset);
	LOG("panicreset: panic restart through CF9 full reset (debug.rmb_panicreset=%d, panic actions %s)",
	    gPanicReset, gRunPanicActions ? "found" : "missing");
}
