//
//  Hardware video (VCN) bring-up experiment, `-rmbvcn` only (GMKtec M8, Radeon 660M).
//
//  Without it VCN hangs the whole GPU (display.cpp noVideoEngine). Causes and fixes:
//  1. VCN is power gated: smu.cpp sends PowerUpVcn/PowerUpJpeg at smu_hw_init.
//  2. HWLibs addresses the MM hub through two 168-entry tables of Navi MMHUB 2.0 VM register byte
//     addresses (page table base/start/end per context, invalidate engine req/ack/sem/range).
//     On Rembrandt's MMHUB 2.3 those offsets are other registers, so the VMPT job's invalidate-ack
//     poll never ends. Rewrite every run of MMHUB 2.0 VM addresses to the 2.3 ones (by name).
//  3. HWLibs embeds Navi's VCN 3.0 image as ativvaxy_vcn3.dat and has a VCN 3.1 name
//     (ativvaxy_vcn3_1.dat) but no image; Rembrandt is VCN 3.1.2. Hand it Rembrandt's PSP-signed
//     image (Linux vcn_3_1_2.bin minus its 0x100-byte amdgpu header, same layout as Apple's blob).
//  4. The accelerator fills the MM hub VM register addresses the SDMA VMPT job writes as MMHUB 2.0
//     offsets (wrapFillVm), which hung SDMA; rewritten to MMHUB 2.3.
//  5. MMMC_VM_MX_L1_TLB_CNTL.ENABLE_ADVANCED_DRIVER_MODEL is left off, so the MM hub translated VCN's
//     accesses in VMID 0 and the decode IB (a user VMID address) faulted: black output (vcnMmHubAdm).
//  6. VCN's completion interrupt never reaches the accelerator here, so a decode was only seen as done
//     on the next display vblank: every frame took 16.7 or 33.3 ms (30 fps). While something waits
//     for a stamp, a thread calls AMDSWScheduler::checkTimestamps every millisecond (stampPoll).
//

#include "rmb.hpp"
#include "fw.hpp"
#include "mmhubremap.hpp"
#include "mmhub2gc.hpp"
#include <Headers/kern_api.hpp>
#include <sys/vnode.h>
#include <kern/thread.h>
#include <sys/fcntl.h>

namespace {

constexpr const char *Vcn3Name = "ativvaxy_vcn3.dat";
constexpr const char *Vcn31Name = "ativvaxy_vcn3_1.dat";

// The SDMA VMPT job's MM hub writes never land on Rembrandt and its MM invalidate-ack poll hangs
// SDMA0_PAGE, so by default point those table entries at the same registers of the GC hub (whose
// writes and acks work; same per-VMID page tables) and keep the MM hub in sync from the CPU
// (vcnMmMirror). -rmbmmto23 = remap to the MMHUB 2.3 registers instead (hangs).
bool lookup(uint32_t byteAddr, uint32_t &out) {
	if (byteAddr & 3)
		return false;
	auto dw = byteAddr >> 2;
	static int to23 = -1;
	if (to23 < 0)
		to23 = checkKernelArgument("-rmbmmto23") ? 1 : 0;
	if (to23) {
		for (auto &r : mmhubRemap)
			if (r.from == dw) {
				out = r.to << 2;
				return true;
			}
		return false;
	}
	for (auto &r : mmhubToGc)
		if (r.from == dw) {
			out = r.to << 2;
			return true;
		}
	return false;
}

// Rewrite runs of >= 32 consecutive MMHUB 2.0 VM register byte addresses (the 168-entry tables).
void remapMmhubTables(mach_vm_address_t address, size_t size) {
	auto p = reinterpret_cast<uint32_t *>(address);
	size_t n = size / 4, tables = 0, entries = 0;
	if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS) {
		LOG("vcn: mmhub: kernel writing failed");
		return;
	}
	for (size_t i = 0; i < n;) {
		size_t j = i;
		uint32_t tmp;
		while (j < n && lookup(p[j], tmp))
			j++;
		if (j - i >= 32) {
			for (size_t k = i; k < j; k++) {
				lookup(p[k], tmp);
				p[k] = tmp;
			}
			LOG("vcn: mmhub: table at +%#lx, %zu entries remapped", (i * 4), j - i);
			tables++, entries += j - i;
		}
		i = j > i ? j : i + 1;
	}
	MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
	LOG("vcn: mmhub: %zu tables, %zu register addresses remapped", tables, entries);
}

using CreateFw = uint64_t (*)(const uint8_t *, uint32_t, uint32_t, const char *);
mach_vm_address_t orgCreateFw;

uint64_t wrapCreateFw(const uint8_t *data, uint32_t size, uint32_t flags, const char *name) {
	// The per-device-type firmware directory is full (putFirmware of an extra entry fails for the
	// Navi 2x type), so take over the VCN 3.0 slot: same entry, Rembrandt image, VCN 3.1 name.
	if (name && !strcmp(name, Vcn3Name)) {
		LOG("vcn: fw: %s %#x bytes -> %s (yellow_carp VCN 3.1.2, %#x bytes)", name, size, Vcn31Name, FW_SIZE(vcn));
		data = fw_yc_vcn, size = FW_SIZE(vcn), name = Vcn31Name;
	}
	return reinterpret_cast<CreateFw>(orgCreateFw)(data, size, flags, name);
}


// ---- -rmbvcntrace: VCN SWIP trace (register access through the SWIP's cgs wrappers + entry points).
// internal_cgs_write_register(ctx, seg, off, val) / read_register(ctx, seg, off): absolute dword
// address = off + base[seg], base table at *(ctx+0x10)+0x34.
uint32_t vcnAbs(uint64_t ctx, uint64_t seg, uint64_t off) {
	auto inst = *reinterpret_cast<uint64_t *>(ctx + 0x10);
	return static_cast<uint32_t>(off) + *reinterpret_cast<uint32_t *>(inst + 0x34 + (seg & 0xFFFFFFFF) * 4);
}

uint64_t gVcnInst;  // VCN SWIP instance (ctx+0x10)

// Experiment switches, re-read every second from /Users/user/m8/vcn-mode (hex) by the watch thread:
// 1 = NOP out Apple's TSC command (0xf) in the IB packet      2 = Linux fw_shared present_flag_0
// 4 = no bit 31 in fw_shared rb.wptr / UVD_SCRATCH2            8 = UVD_LMI_CTRL2 = 0 in the DPG list
// 0x10 = smu_interface_type 2 (+ SMU_VERSION_INFO flag, fw_shared size 0x1000)
// 0x20 = IB packet in Linux order (vmid, bar low, bar high)   0x40 = no extra DPG list entries
volatile uint32_t gVcnMode;
uint32_t gVcnDumped, gIbVmid;
uint64_t gIbAddr;

// The fix for the black output: HWLibs' MMHUB 2.3 init leaves MMMC_VM_MX_L1_TLB_CNTL at 0x1d19, i.e.
// ENABLE_ADVANCED_DRIVER_MODEL (bit 6) off, and then the MM hub translates every client access in
// VMID 0: VCN's firmware (client VCNU) read the IB of the decode job (a VMID n user address) through
// the GART and faulted, so nothing was decoded. Linux mmhub_v2_3_init_tlb_regs sets the bit.
// -rmbnommadm = leave it.
void vcnMmHubAdm(const char *why) {
	if (!mmioAvailable() || checkKernelArgument("-rmbnommadm"))
		return;
	uint32_t v = mmioRead(0x1A000 + 0x8f3);
	if (v == 0xFFFFFFFF || (v & 0x40))
		return;
	mmioWrite(0x1A000 + 0x8f3, v | 0x40);
	static uint32_t logged;
	if (logged++ < 20)
		LOG("vcn: %s: MMMC_VM_MX_L1_TLB_CNTL %x -> %x (ENABLE_ADVANCED_DRIVER_MODEL)", why, v, mmioRead(0x1A000 + 0x8f3));
}

mach_vm_address_t org_mmhub_2_3_0_initialize_registers;
uint64_t wrap_mmhub_2_3_0_initialize_registers(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
	auto r = reinterpret_cast<Fn6>(org_mmhub_2_3_0_initialize_registers)(a, b, c, d, e, f);
	vcnMmHubAdm("mmhub init");
	return r;
}

uint32_t *vcnFwShared() {
	return gVcnInst ? *reinterpret_cast<uint32_t **>(gVcnInst + 0x388) : nullptr;
}

void vcnApplyFwShared() {
	auto fs = vcnFwShared();
	if (!fs)
		return;
	uint32_t mode = gVcnMode;
	if (mode & 2)
		fs[0] = (fs[0] & 0x800) | 0x340, fs[3] = 0;  // RB | MULTI_QUEUE | SW_RING
	if (mode & 0x10)
		fs[0] |= 0x800, fs[0x58 / 4] = 2;
	if (mode & 4)
		fs[0x34 / 4] &= 0x7FFFFFFF;
}
mach_vm_address_t orgCgsWrite, orgCgsRead;
uint32_t gWrites, gReads, gLastReadReg, gLastReadVal, gReadRepeat;

uint64_t wrapCgsWrite(uint64_t ctx, uint64_t seg, uint64_t off, uint64_t val, uint64_t e, uint64_t f) {
	if (gWrites++ < 4000)
		LOG("vcnreg: wr %x = %x", vcnAbs(ctx, seg, off), static_cast<uint32_t>(val));
	if ((gVcnMode & 4) && vcnAbs(ctx, seg, off) == 0x7e16) {
		val &= 0x7FFFFFFF;
		vcnApplyFwShared();
	}
	return reinterpret_cast<Fn6>(orgCgsWrite)(ctx, seg, off, val, e, f);
}

uint64_t wrapCgsRead(uint64_t ctx, uint64_t seg, uint64_t off, uint64_t d, uint64_t e, uint64_t f) {
	auto r = reinterpret_cast<Fn6>(orgCgsRead)(ctx, seg, off, d, e, f);
	auto reg = vcnAbs(ctx, seg, off);
	auto v = static_cast<uint32_t>(r);
	if (reg == gLastReadReg && v == gLastReadVal) {
		gReadRepeat++;
		return r;
	}
	if (gReads++ < 4000) {
		if (gReadRepeat)
			LOG("vcnreg: (previous read repeated %u times)", gReadRepeat);
		LOG("vcnreg: rd %x = %x", reg, v);
	}
	gLastReadReg = reg, gLastReadVal = v, gReadRepeat = 0;
	return r;
}

#define VCN_TRACE(name)                                                                        \
	mach_vm_address_t org_##name;                                                              \
	uint64_t wrap_##name(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) { \
		LOG("vcn>> " #name "(%llx, %llx, %llx, %llx, %llx)", a, b, c, d, e);                   \
		auto r = reinterpret_cast<Fn6>(org_##name)(a, b, c, d, e, f);                          \
		LOG("vcn<< " #name " = %llx", r);                                                      \
		return r;                                                                              \
	}

VCN_TRACE(vcn_sw_init)
VCN_TRACE(vcn_hw_init)
VCN_TRACE(engine_hw_init)
VCN_TRACE(ipi_psp_load_fw)
VCN_TRACE(ipiPspVCNSramUpdate)
VCN_TRACE(psp_np_fw_update_vcn_sram)
VCN_TRACE(psp_np_fw_load_service)
mach_vm_address_t org_internal_cgs_query_fw_load_status;
uint64_t wrap_internal_cgs_query_fw_load_status(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
	auto r = reinterpret_cast<Fn6>(org_internal_cgs_query_fw_load_status)(a, b, c, d, e, f);
	auto o = reinterpret_cast<uint8_t *>(c);
	LOG("vcn: fw load status(type %llu) = %llx: loaded %u/%u addr %llx", b & 0xFFFFFFFF, r, o ? o[0] : 0, o ? o[1] : 0,
	    o ? *reinterpret_cast<uint64_t *>(o + 8) : 0);
	return r;
}

// internal_cos_read_fw(ctx, req): req = {buffer, capacity, name}; returns the size read (0 = failed).
mach_vm_address_t org_internal_cos_read_fw;
uint64_t wrap_internal_cos_read_fw(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
	auto r = reinterpret_cast<Fn6>(org_internal_cos_read_fw)(a, b, c, d, e, f);
	auto name = b ? *reinterpret_cast<const char **>(b + 0x10) : nullptr;
	LOG("vcn: read_fw(%s) = %#llx", name ? name : "?", r & 0xFFFFFFFF);
	return r;
}
VCN_TRACE(internal_cgs_submit_sram)
VCN_TRACE(vcn_start_queue)
mach_vm_address_t org_vcn_submit_frame;
uint64_t wrap_vcn_submit_frame(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
	vcnMmMirror("submit");
	vcnApplyFwShared();
	LOG("vcn>> vcn_submit_frame (mode %x)", gVcnMode);
	auto r = reinterpret_cast<Fn6>(org_vcn_submit_frame)(a, b, c, d, e, f);
	LOG("vcn<< vcn_submit_frame = %llx", r);
	// First submits: dump the decode ring (VMID 0, base = UVD_LMI_RBC_RB_64BIT_BAR).
	if (gVcnDumped < 2) {
		gVcnDumped++;
		// fw_shared (UVD_LMI_VCPU_NC0_64BIT_BAR, 0x58 bytes): present_flag_0, rb ptrs, multi_queue,
		// sw_ring, fw_log, smu_interface_info (Linux amdgpu_fw_shared).
		uint64_t nc0 = gVcnInst ? *reinterpret_cast<uint64_t *>(gVcnInst + 0x378) : 0;
		IODelay(300);
		LOG("vcnstate: IB_VMID %x RB_VMID %x IB bar %x_%x size %x XX_MASK %x IB_REG_CHECK %x ARB %x LMI_CTRL2 %x POWER %x", mmioRead(0x82b1),
		    mmioRead(0x82b0), mmioRead(0x8235), mmioRead(0x8234), mmioRead(0x805c), mmioRead(0x806c), mmioRead(0x806b),
		    mmioRead(0x7ec6), mmioRead(0x82a6), mmioRead(0x7e04));
		uint32_t fs[24] {};
		if (nc0 && nc0 != 0xdeadbeefdeadbeefULL && gartRead(nc0, fs, 24))
			for (uint32_t i = 0; i < 24; i += 8)
				LOG("vcnshared %llx +%02x: %08x %08x %08x %08x %08x %08x %08x %08x", nc0, i * 4, fs[i], fs[i + 1], fs[i + 2],
				    fs[i + 3], fs[i + 4], fs[i + 5], fs[i + 6], fs[i + 7]);
		else
			LOG("vcnshared: NC0 %llx not readable", nc0);
		rmbDumpMmVa("vcnib", gIbVmid, gIbAddr, 64);
		rmbDumpMmVa("vcnib", gIbVmid, gIbAddr + 0x100, 64);
		rmbDumpMmVa("vcnib", gIbVmid, gIbAddr + 0x200, 48);
		uint64_t rb = (static_cast<uint64_t>(mmioRead(0x8233)) << 32) | mmioRead(0x8232);
		LOG("vcnring: base %llx rptr %x wptr %x scratch2 %x", rb, mmioRead(0x80e0), mmioRead(0x80e1), mmioRead(0x7e16));
		uint32_t d[64] {};
		if (rb && rb != 0xdeadbeefdeadbeefULL && gartRead(rb, d, 64))
			for (uint32_t i = 0; i < 64; i += 8)
				LOG("vcnring +%03x: %08x %08x %08x %08x %08x %08x %08x %08x", i * 4, d[i], d[i + 1], d[i + 2], d[i + 3], d[i + 4],
				    d[i + 5], d[i + 6], d[i + 7]);
	}
	return r;
}
VCN_TRACE(vcn_wait_on_reg_read)
VCN_TRACE(vcn_wait_on_reg_read_ms)
VCN_TRACE(wait_for_vcn_response)
VCN_TRACE(vcn_wait_on_queue_status)
VCN_TRACE(engine_3_0_dpg_secure_initialize)
VCN_TRACE(engine_3_0_dpg_unsecure_initialize)
VCN_TRACE(engine_3_0_pause_dpg)
VCN_TRACE(engine_wait_vcn_fw_loaded)
VCN_TRACE(mm_sched_3_0_start)
VCN_TRACE(engine_3_0_static_initialize)
VCN_TRACE(engine_3_0_disable_power_gating)
VCN_TRACE(engine_3_0_disable_clock_gating)
VCN_TRACE(engine_3_0_write_register_in_dpg_mode)
VCN_TRACE(engine_3_0_set_fw_entry_info)
VCN_TRACE(engine_3_0_reset)
VCN_TRACE(engine_3_0_get_variant_type)
VCN_TRACE(queue_manager_3_0_initialize)
VCN_TRACE(queue_decode_3_0_init_ring)
VCN_TRACE(queue_decode_3_0_hw_init_ring)
VCN_TRACE(queue_decode_3_0_submit_frame)
VCN_TRACE(queue_decode_3_0_reset_queue)
VCN_TRACE(queue_decode_3_0_wait_for_idle)
VCN_TRACE(engine_3_0_load_parameters)
VCN_TRACE(engine_3_0_disable_uvdrsmu_intf)
VCN_TRACE(engine_3_0_enable_clock_gating)
VCN_TRACE(mmhub_2_0_initialize_registers)
VCN_TRACE(mmhub_2_1_0_initialize_registers)
VCN_TRACE(mmhub_2_2_0_initialize_registers)

// add_to_dpg_sram(ctx, sram, seg, off, val): log the absolute address too.
mach_vm_address_t orgAddDpgSram;
uint32_t gSramAdds;
uint64_t wrapAddDpgSram(uint64_t ctx, uint64_t sram, uint64_t seg, uint64_t off, uint64_t val, uint64_t f) {
	if (gSramAdds++ < 1000)
		LOG("vcnreg: sram %x = %x", vcnAbs(ctx, seg, off), static_cast<uint32_t>(val));
	return reinterpret_cast<Fn6>(orgAddDpgSram)(ctx, sram, seg, off, val, f);
}

// Linux vcn_v3_0_start_dpg_mode also programs UVD_REG_XX_MASK = 0x10, UVD_RBC_XX_IB_REG_CHECK = 3
// (which registers ring/IB packets may write, e.g. LMI_RBC_IB_VMID) and UVD_RB_ARB_CTRL = 0 (unblock
// VCPU register access); Apple's VCN 3.0 list has none of them. Decoding works without them, so they
// are appended only with -rmbxxmask.
// The list is {internal offset, value} pairs at inst+0x340 (CPU address), byte size at inst+0x358,
// capacity at inst+0x320; it ends with MASTINT_EN, NO_OP (the PSP size check workaround), so the new
// entries go in front of a new closing NO_OP.
mach_vm_address_t org_engine_3_0_decode_sram_secure_initialize;
uint64_t wrap_engine_3_0_decode_sram_secure_initialize(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
	auto r = reinterpret_cast<Fn6>(org_engine_3_0_decode_sram_secure_initialize)(a, b, c, d, e, f);
	auto inst = *reinterpret_cast<uint64_t *>(a + 0x10);
	gVcnInst = inst;
	vcnMmHubAdm("decode start");
	if (r || !inst || (gVcnMode & 0x40) || !checkKernelArgument("-rmbxxmask")) {
		LOG("vcn: decode sram = %llx (no extra entries)", r);
		return r;
	}
	auto list = *reinterpret_cast<uint32_t **>(inst + 0x340);
	auto &size = *reinterpret_cast<uint32_t *>(inst + 0x358);
	auto cap = *reinterpret_cast<uint64_t *>(inst + 0x320);
	static const struct { uint32_t off, val; } extra[] {{0x26c, 0x10}, {0x26b, 0x3}, {0xc6, 0}, {0xa9, 0}};
	if (!list || size < 16 || size + sizeof(extra) > cap) {
		LOG("vcn: decode sram: no room (size %x cap %llx)", size, cap);
		return r;
	}
	uint32_t n = size / 4;
	uint32_t mode = gVcnMode;
	for (uint32_t i = 0; i + 1 < n; i += 2) {
		if ((mode & 8) && list[i] == 0x426)  // UVD_LMI_CTRL2 (0x4a6 -> internal)
			LOG("vcn: decode sram: LMI_CTRL2 %x -> 0", list[i + 1]), list[i + 1] = 0;
		if ((mode & 0x10) && list[i] == 0xd3)  // UVD_VCPU_NONCACHE_SIZE0 (0x153 -> internal)
			LOG("vcn: decode sram: NONCACHE_SIZE0 %x -> 1000", list[i + 1]), list[i + 1] = 0x1000;
	}
	vcnApplyFwShared();
	for (auto &x : extra) {
		// As engine_3_0_add_to_dpg_sram: SOC byte address -> VCN internal offset (video / AON range).
		uint32_t addr = (vcnAbs(a, 1, x.off) << 2) & 0xFFFFC;
		if (addr - 0x1fa00 < 0x2600)
			addr -= 0x1fa00;
		else if (addr - 0x1f800 < 0x600)
			addr += 0x10800;
		list[n++] = addr >> 2, list[n++] = x.val;
	}
	LOG("vcn: decode sram: %x -> %x bytes (cap %llx): REG_XX_MASK [%x]=%x RBC_XX_IB_REG_CHECK [%x]=%x RB_ARB_CTRL [%x]=%x NO_OP [%x]", size,
	    n * 4, cap, list[n - 8], list[n - 7], list[n - 6], list[n - 5], list[n - 4], list[n - 3], list[n - 2]);
	size = n * 4;
	return r;
}

// queue_decode_3_0_build_command(queue, req, out): the IB packet is {0x431 vmid, 0x3b5 hi, 0x3b4 lo,
// 0x10 TSC lo, 0x11 TSC hi, 0xf 0x8000001e, 0x25c size} as (register, value) pairs, filled in steps.
mach_vm_address_t org_queue_decode_3_0_build_command;
uint64_t wrap_queue_decode_3_0_build_command(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
	auto o = reinterpret_cast<uint32_t *>(c);
	auto swap = [o]() {
		uint32_t r = o[2], v = o[3];
		o[2] = o[4], o[3] = o[5], o[4] = r, o[5] = v;
	};
	bool ib = b && *reinterpret_cast<uint32_t *>(b) == 0x10002;
	if (ib && o[0] == 0x431 && o[2] == 0x3b4 && o[4] == 0x3b5)
		swap();
	auto r = reinterpret_cast<Fn6>(org_queue_decode_3_0_build_command)(a, b, c, d, e, f);
	if (!ib || o[0] != 0x431 || o[2] != 0x3b5)
		return r;
	uint32_t mode = gVcnMode;
	vcnMmHubAdm("submit");  // the accelerator rewrites the register (0x1d19) after the HWLibs init
	gIbVmid = o[1], gIbAddr = (static_cast<uint64_t>(o[3]) << 32) | o[5];
	if (mode & 1)
		o[6] = 0x29, o[7] = 0, o[8] = 0x29, o[9] = 0, o[10] = 0x29, o[11] = 0;
	if (mode & 0x20)
		swap();
	return r;
}

#define VCN_REQ(name) {"_" #name, wrap_##name, org_##name}

void vcnTraceRoute(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	KernelPatcher::RouteRequest reqs[] {
		{"_internal_cgs_write_register", wrapCgsWrite, orgCgsWrite},
		{"_internal_cgs_read_register", wrapCgsRead, orgCgsRead},
		{"_engine_3_0_add_to_dpg_sram", wrapAddDpgSram, orgAddDpgSram},
		VCN_REQ(vcn_sw_init),
		VCN_REQ(vcn_hw_init),
		VCN_REQ(engine_hw_init),
		VCN_REQ(ipi_psp_load_fw),
		VCN_REQ(ipiPspVCNSramUpdate),
		VCN_REQ(psp_np_fw_update_vcn_sram),
		VCN_REQ(psp_np_fw_load_service),
		VCN_REQ(internal_cgs_query_fw_load_status),
		VCN_REQ(internal_cos_read_fw),
		VCN_REQ(internal_cgs_submit_sram),
		VCN_REQ(vcn_start_queue),
		VCN_REQ(vcn_submit_frame),
		VCN_REQ(vcn_wait_on_reg_read),
		VCN_REQ(vcn_wait_on_reg_read_ms),
		VCN_REQ(wait_for_vcn_response),
		VCN_REQ(vcn_wait_on_queue_status),
		VCN_REQ(engine_3_0_dpg_secure_initialize),
		VCN_REQ(engine_3_0_dpg_unsecure_initialize),
		VCN_REQ(engine_3_0_pause_dpg),
		VCN_REQ(engine_wait_vcn_fw_loaded),
		VCN_REQ(mm_sched_3_0_start),
		VCN_REQ(engine_3_0_static_initialize),
		VCN_REQ(engine_3_0_disable_power_gating),
		VCN_REQ(engine_3_0_disable_clock_gating),
		VCN_REQ(engine_3_0_write_register_in_dpg_mode),
		VCN_REQ(engine_3_0_set_fw_entry_info),
		VCN_REQ(engine_3_0_reset),
		VCN_REQ(engine_3_0_get_variant_type),
		VCN_REQ(queue_manager_3_0_initialize),
		VCN_REQ(queue_decode_3_0_init_ring),
		VCN_REQ(queue_decode_3_0_hw_init_ring),
		VCN_REQ(queue_decode_3_0_submit_frame),
		VCN_REQ(queue_decode_3_0_reset_queue),
		VCN_REQ(queue_decode_3_0_wait_for_idle),
		VCN_REQ(engine_3_0_load_parameters),
		VCN_REQ(engine_3_0_disable_uvdrsmu_intf),
		VCN_REQ(engine_3_0_enable_clock_gating),
		VCN_REQ(mmhub_2_0_initialize_registers),
		VCN_REQ(mmhub_2_1_0_initialize_registers),
		VCN_REQ(mmhub_2_2_0_initialize_registers),
	};
	size_t ok = 0;
	for (auto &r : reqs) {
		if (patcher.routeMultiple(index, &r, 1, address, size))
			ok++;
		else
			LOG("vcntrace: failed to route %s (err %d)", r.symbol, patcher.getError());
		patcher.clearError();
	}
	LOG("vcntrace: %zu/%zu hooks", ok, arrsize(reqs));
}

}  // namespace

// MMHUB 2.3 user contexts vs the GC hub's: VCN runs in the client's VMID through the MM hub.
// MMVM_CONTEXTn_CNTL 0x740+n, PT base/start/end 0x940+8n (+0/+2/+4); GCVM: 0x15fc+n, 0x1667/87/a7+2n.
void vcnVmContexts(const char *why) {
	constexpr uint32_t G = 0x1260, Mm = 0x1A000;
	auto g = [](uint32_t o) { return mmioRead(G + o); };
	auto m = [](uint32_t o) { return mmioRead(Mm + o); };
	LOG("vmctx %s: MM CONTEXTS_DISABLE %x L2_CNTL %x; GC CONTEXTS_DISABLE %x", why, m(0x750), m(0x700), g(0x160c));
	for (uint32_t v = 0; v < 16; v++) {
		uint32_t gcCntl = g(0x15fc + v), mmCntl = m(0x740 + v);
		if (!(gcCntl & 1) && !(mmCntl & 1))
			continue;
		LOG("vmctx %s: vmid %u GC cntl %x base %x_%x start %x_%x end %x_%x | MM cntl %x base %x_%x start %x_%x end %x_%x",
		    why, v, gcCntl, g(0x1668 + 2 * v), g(0x1667 + 2 * v), g(0x1688 + 2 * v), g(0x1687 + 2 * v), g(0x16a8 + 2 * v),
		    g(0x16a7 + 2 * v), mmCntl, m(0x941 + 8 * v), m(0x940 + 8 * v), m(0x943 + 8 * v), m(0x942 + 8 * v),
		    m(0x945 + 8 * v), m(0x944 + 8 * v));
	}
}

// -rmbvcnarm at arm time: enable MM hub user contexts 1-15 with the GC hub's CNTL (PT depth/block
// size/fault bits), as Linux mmhub_v2_3_setup_vmid_config does at init. -rmbnommctx = leave them.
void vcnEnableMmContexts() {
	vcnVmContexts("before mmctx");
	if (checkKernelArgument("-rmbnommctx"))
		return;
	constexpr uint32_t G = 0x1260, Mm = 0x1A000;
	unsigned n = 0;
	for (uint32_t v = 1; v < 16; v++) {
		uint32_t gcCntl = mmioRead(G + 0x15fc + v), mmCntl = mmioRead(Mm + 0x740 + v);
		if ((mmCntl & 1) || !(gcCntl & 1))
			continue;
		mmioWrite(Mm + 0x740 + v, gcCntl);
		n++;
	}
	LOG("mmctx: enabled %u MM hub user contexts from the GC hub", n);
	vcnVmContexts("after mmctx");
}

// The SDMA VMPT job's MM hub register writes do not land on Rembrandt (MM PT bases stay 0 while
// the GC hub's get set), so mirror the GC hub's per-VMID page table (same PD for both hubs, as in
// Linux) into the MM hub from the CPU and invalidate the MM hub over MMIO (gmc_v10_0_flush_vm_hub,
// engine 17 with the semaphore). -rmbnommmirror = off.
void vcnMmMirror(const char *why) {
	// Off by default: the MM hub's VMIDs are allocated apart from the GC hub's, so copying GC VMID n
	// over MM VMID n clobbers the VCN client's page table; with fillVMRegisters remapped the SDMA
	// VMPT job programs the MM hub itself. -rmbmmmirror = on (debug).
	if (!vcnEnabled() || !checkKernelArgument("-rmbmmmirror"))
		return;
	constexpr uint32_t G = 0x1260, Mm = 0x1A000;
	auto g = [](uint32_t o) { return mmioRead(G + o); };
	auto m = [](uint32_t o) { return mmioRead(Mm + o); };
	uint32_t mask = 0;
	for (uint32_t v = 1; v < 16; v++) {
		uint32_t regs[6] = {g(0x1667 + 2 * v), g(0x1668 + 2 * v), g(0x1687 + 2 * v), g(0x1688 + 2 * v), g(0x16a7 + 2 * v),
		                    g(0x16a8 + 2 * v)};
		if (!regs[0] && !regs[1])
			continue;
		bool same = true;
		for (uint32_t i = 0; i < 6; i++)
			same &= m(0x940 + 8 * v + i) == regs[i];
		if ((m(0x740 + v) & 1) == 0)
			mmioWrite(Mm + 0x740 + v, g(0x15fc + v)), same = false;
		if (same)
			continue;
		for (uint32_t i = 0; i < 6; i++)
			mmioWrite(Mm + 0x940 + 8 * v + i, regs[i]);
		mask |= 1u << v;
	}
	if (!mask)
		return;
	// Invalidate: PER_VMID_INVALIDATE_REQ mask, flush type 0, L2 PTEs/PDE0-2, L1 PTEs.
	constexpr uint32_t Sem = 0xa00 + 8 * 17, Req = 0xa01 + 8 * 17, Ack = 0xa02 + 8 * 17;
	uint32_t req = mask | (1u << 19) | (1u << 20) | (1u << 21) | (1u << 22) | (1u << 23);
	int i = 0;
	for (; i < 100000 && !(m(Sem) & 1); i++)
		IODelay(1);
	bool gotSem = i < 100000;
	mmioWrite(Mm + Req, req);
	for (i = 0; i < 100000 && (m(Ack) & mask) != mask; i++)
		IODelay(1);
	uint32_t ack = m(Ack);
	if (gotSem)
		mmioWrite(Mm + Sem, 0);
	static uint32_t logged;
	if (logged++ < 40)
		LOG("mmmirror %s: vmid mask %x -> MM hub, invalidate %s (sem %d, ack %x after %d us)", why, mask,
		    (ack & mask) == mask ? "ok" : "TIMEOUT", gotSem, ack, i);
}

extern "C" unsigned int ml_phys_read_64(addr64_t paddr64);

// Reads n dwords at VMID 0 GPU VA va. VMID 0 is a flat (depth 0) table over [start, end]: PTE index =
// page - start (vmwatch's walk masks it to the PTB size, which only fits multi-level tables).
bool gartRead(uint64_t va, uint32_t *out, uint32_t n) {
	constexpr uint32_t G = 0x1260;
	auto g = [](uint32_t o) { return mmioRead(G + o); };
	uint64_t fbPhys = static_cast<uint64_t>(g(0x16e7) & 0xFFFFFF) << 24;
	uint64_t base = ((static_cast<uint64_t>(g(0x1668)) << 32) | g(0x1667)) & 0x0000FFFFFFFFF000ULL;
	uint64_t start = (static_cast<uint64_t>(g(0x1688)) << 32) | g(0x1687);
	uint64_t end = (static_cast<uint64_t>(g(0x16a8)) << 32) | g(0x16a7);
	uint64_t page = va >> 12;
	if (page < start || page > end || base < fbPhys)
		return false;
	uint64_t pte = vramRead64(base - fbPhys + (page - start) * 8);
	LOG("gart: va %llx page %llx -> pte %llx (base %llx start %llx)", va, page, pte, base, start);
	if (pte == ~0ULL || !(pte & 1))
		return false;
	uint64_t pa = (pte & 0x0000FFFFFFFFF000ULL) + (va & 0xFFF);
	bool sys = pte & 2;
	for (uint32_t i = 0; i < n; i++) {
		uint64_t a = pa + i * 4;
		if ((a & ~0xFFFULL) != (pa & ~0xFFFULL))
			return true;  // stay in the page
		out[i] = sys ? ml_phys_read_64(a) : static_cast<uint32_t>(vramRead64(a - fbPhys));
	}
	return true;
}

// -rmbvcnarm: after arming, watch SDMA0's page queue from a thread of its own; when its IB read
// pointer sits still for 1 s, dump that IB (VMID 0) to see which packet it is stuck on.
namespace {
void vcnReadMode() {
	auto ctx = vfs_context_create(nullptr);
	vnode_t vp = nullptr;
	uint32_t mode = 0;
	if (!vnode_open("/Users/user/m8/vcn-mode", FREAD, 0, 0, &vp, ctx)) {
		char buf[16] {};
		int resid = 0;
		if (!vn_rdwr(UIO_READ, vp, buf, sizeof(buf) - 1, 0, UIO_SYSSPACE, 0, vfs_context_ucred(ctx), &resid, vfs_context_proc(ctx)))
			for (char ch : buf) {
				if (ch >= '0' && ch <= '9')
					mode = mode * 16 + (ch - '0');
				else if (ch >= 'a' && ch <= 'f')
					mode = mode * 16 + (ch - 'a' + 10);
				else
					break;
			}
		vnode_close(vp, FREAD, ctx);
	}
	vfs_context_rele(ctx);
	if (mode != gVcnMode) {
		LOG("vcnmode: %x -> %x", gVcnMode, mode);
		gVcnMode = mode;
		gVcnDumped = 0;
	}
}
}  // namespace

namespace {
// Debug pokes (-rmbvcnarm only): /Users/user/m8/vcn-poke holds lines "r ADDR [COUNT]" / "w ADDR VALUE"
// (hex dword MMIO addresses); run once by the watch thread, results in the log, file emptied.
void vcnPoke() {
	constexpr const char *Path = "/Users/user/m8/vcn-poke";
	auto ctx = vfs_context_create(nullptr);
	vnode_t vp = nullptr;
	static char buf[1024];
	int got = 0;
	if (!vnode_open(Path, FREAD | FWRITE, 0, 0, &vp, ctx)) {
		int resid = 0;
		if (!vn_rdwr(UIO_READ, vp, buf, sizeof(buf) - 1, 0, UIO_SYSSPACE, 0, vfs_context_ucred(ctx), &resid, vfs_context_proc(ctx)))
			got = static_cast<int>(sizeof(buf) - 1) - resid;
		if (got > 0) {
			struct vnode_attr va;
			VATTR_INIT(&va);
			VATTR_SET(&va, va_data_size, 0);
			vnode_setattr(vp, &va, ctx);
		}
		vnode_close(vp, FREAD | FWRITE, ctx);
	}
	vfs_context_rele(ctx);
	if (got <= 0)
		return;
	buf[got] = 0;
	const char *p = buf;
	auto hex = [&p](uint32_t &v) {
		while (*p == ' ')
			p++;
		v = 0;
		bool any = false;
		for (;; p++) {
			char ch = *p;
			uint32_t d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : 16;
			if (d > 15)
				break;
			v = v * 16 + d, any = true;
		}
		return any;
	};
	while (*p) {
		char op = *p++;
		uint32_t addr = 0, val = 0;
		if ((op == 'r' || op == 'w') && hex(addr)) {
			bool hasVal = hex(val);
			if (op == 'w' && hasVal) {
				uint32_t before = mmioRead(addr);
				mmioWrite(addr, val);
				LOG("poke: w %x = %x (was %x, now %x)", addr, val, before, mmioRead(addr));
			} else if (op == 'r') {
				uint32_t n = hasVal && val && val <= 64 ? val : 1;
				for (uint32_t i = 0; i < n; i += 4)
					LOG("poke: r %x: %08x %08x %08x %08x", addr + i, mmioRead(addr + i), i + 1 < n ? mmioRead(addr + i + 1) : 0,
					    i + 2 < n ? mmioRead(addr + i + 2) : 0, i + 3 < n ? mmioRead(addr + i + 3) : 0);
			}
		}
		while (*p && *p != '\n')
			p++;
		if (*p)
			p++;
	}
}
}  // namespace

void sdmaWatch(void *, wait_result_t) {
	constexpr uint32_t S = 0x1260;
	auto s = [](uint32_t o) { return mmioRead(S + o); };
	uint64_t lastIb = 0;
	uint32_t lastRp = ~0u, still = 0, dumps = 0;
	uint32_t lastFault = 0, faults = 0;
	for (int t = 0; t < 72000; t++) {
		IOSleep(100);
		if (t % 10 == 0) {
			vcnReadMode();
			vcnPoke();
		}
		// MM hub L2 protection faults (VCN's memory accesses): log each new one, then clear it
		// (MMVM_L2_PROTECTION_FAULT_CNTL bit 0 = CLEAR_PROTECTION_FAULT_STATUS_ADDR).
		uint32_t fs = mmioRead(0x1A000 + 0x70c);
		if (fs && faults < 300) {
			faults++;
			LOG("mmfault: status %x (cid %x rw %u vmid %u map %u perm %x walker %x) addr %x_%x000", fs, (fs >> 9) & 0x1ff,
			    (fs >> 18) & 1, (fs >> 20) & 0xf, (fs >> 8) & 1, (fs >> 4) & 0xf, (fs >> 1) & 7, mmioRead(0x1A000 + 0x70e),
			    mmioRead(0x1A000 + 0x70d));
			if (faults <= 2)
				vcnVmContexts("fault");
			uint32_t c = mmioRead(0x1A000 + 0x708) & ~1u;
			mmioWrite(0x1A000 + 0x708, c | 1);
			mmioWrite(0x1A000 + 0x708, c);
		}
		lastFault = fs;
		if (dumps >= 3)
			continue;
		uint32_t cntl = s(0xe2), rp = s(0xe3), size = s(0xe7);
		uint64_t ib = (static_cast<uint64_t>(s(0xe6)) << 32) | s(0xe5);
		if (!(cntl & 1) || ib != lastIb || rp != lastRp) {
			lastIb = ib, lastRp = rp, still = 0;
			continue;
		}
		if (++still != 10)
			continue;
		LOG("sdmawatch: IB %llx size %x stuck at rptr %x (cntl %x, RB rptr %x wptr %x, status %x)", ib, size, rp, cntl, s(0xdb),
		    s(0xdd), s(0x100));
		uint32_t d[64] {};
		uint32_t cnt = size && size <= 64 ? size : 64;
		if (!gartRead(ib, d, cnt)) {
			LOG("sdmawatch: IB not readable");
		} else {
			for (uint32_t i = 0; i < cnt; i += 8)
				LOG("sdmawatch ib +%03x: %08x %08x %08x %08x %08x %08x %08x %08x", i * 4, d[i], d[i + 1], d[i + 2], d[i + 3],
				    d[i + 4], d[i + 5], d[i + 6], d[i + 7]);
		}
		dumps++;
	}
	LOG("sdmawatch: done");
}

void vcnStartSdmaWatch() {
	thread_t t;
	if (kernel_thread_start(sdmaWatch, nullptr, &t) == KERN_SUCCESS)
		thread_deallocate(t);
}

// At a channel restart (-rmbvcn): what is SDMA0's page queue stuck on, and the MMHUB 2.3
// invalidation engines / L2 fault state (VMPT jobs invalidate the MM hub through SDMA).
void vcnHangDump(const char *why) {
	if (!vcnEnabled())
		return;
	static uint32_t dumps;
	if (dumps++ >= 4)
		return;
	constexpr uint32_t S = 0x1260, Mm = 0x1A000;
	auto s = [](uint32_t o) { return mmioRead(S + o); };
	auto m = [](uint32_t o) { return mmioRead(Mm + o); };
	uint64_t base = (static_cast<uint64_t>(s(0xda)) << 40) | (static_cast<uint64_t>(s(0xd9)) << 8);
	uint32_t rptr = s(0xdb), wptr = s(0xdd);
	LOG("vcnhang %s: SDMA0 status %x status1 %x page: cntl %x base %llx rptr %x_%x wptr %x_%x status %x ctx %x ib cntl %x rptr %x base %x_%x size %x",
	    why, s(0x25), s(0x26), s(0xd8), base, s(0xdc), rptr, s(0xde), wptr, s(0x100), s(0xe9), s(0xe2), s(0xe3), s(0xe6),
	    s(0xe5), s(0xe7));
	char buf[600];
	int n = 0;
	for (uint32_t e = 0; e < 18; e++) {
		// ENGx_SEM is not read: reading it acquires the semaphore.
		uint32_t req = m(0xa01 + 8 * e), ack = m(0xa02 + 8 * e);
		if (req || ack)
			n += snprintf(buf + n, sizeof(buf) - n, " e%u:r%x a%x", e, req, ack);
	}
	LOG("vcnhang %s: MMHUB inv engines%s; L2 fault status %x addr %x_%x", why, n ? buf : " idle", m(0x70c), m(0x70e),
	    m(0x70d));
	vcnVmContexts(why);
	mmhubDump(why);
}

// AMDRadeonX6000_AMDMMHub_2_x::fillVMRegisters(AMD_GFX10_VM_REGS_PER_HUB *) fills the MM hub VM
// register addresses the SDMA VMPT job writes (per-context PT base/start/end/CNTL, invalidation
// engines) as MMHUB 2.0 offsets on the IP-discovery MMHUB base (0x13200 on Rembrandt). Rembrandt's
// MM hub is MMHUB 2.3 at 0x1A000: rewrite every entry by register name (mmhubRemap).
// Layout (dwords): +0x00..0x2c globals, +0x30 16 x {base lo/hi, start lo/hi, end lo/hi, cntl},
// +0x1f0 18 x {range lo/hi, req, ack, sem}; 0x358 bytes. -rmbnommfill = leave it.
namespace {
mach_vm_address_t orgFillVm[4];
template <size_t I>
uint64_t wrapFillVm(uint64_t self, uint64_t regs, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
	auto r = reinterpret_cast<Fn6>(orgFillVm[I])(self, regs, c, d, e, f);
	if (!regs || !(r & 0xFF) || checkKernelArgument("-rmbnommfill"))
		return r;
	auto p = reinterpret_cast<uint32_t *>(regs);
	uint32_t base = p[0x30 / 4] - 0x72b;  // context 0 PT base lo = base + MMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32
	unsigned done = 0, missed = 0;
	for (uint32_t i = 0; i < 0x358 / 4; i++) {
		uint32_t from = 0x1A000 + (p[i] - base), to = 0;
		bool ok = false;
		for (auto &m : mmhubRemap)
			if (m.from == from) {
				to = m.to, ok = true;
				break;
			}
		if (ok)
			p[i] = to, done++;
		else
			missed++;
	}
	LOG("mmfill %zu: MM hub VM regs base %x -> MMHUB 2.3 @0x1A000: %u remapped, %u left (ctx2 base %x, eng6 req %x ack %x)", I,
	    base, done, missed, p[(0x30 + 2 * 0x1c) / 4], p[(0x1f0 + 6 * 0x14 + 8) / 4], p[(0x1f0 + 6 * 0x14 + 12) / 4]);
	return r;
}
}  // namespace


// ---- Decode completion poll (-rmbnostamppoll = off).
// AMDAccelEventMachine::waitForStamp sleeps until IOAccelEventMachine2::signalStamp wakes it; that
// comes from the channel's timestamp interrupt, or from AMDSWScheduler::checkTimestamps, which reads
// every hardware channel's timestamp. The VCN decode channel's interrupt does not arrive on Rembrandt
// (measured: userland sees a decode complete only at multiples of 16.7 ms, the vblank that runs
// checkTimestamps), so call checkTimestamps ourselves while a waiter has been waiting for over 1 ms.
namespace {
mach_vm_address_t orgEvWaitForStamp, gCheckTimestamps;
volatile SInt32 gStampWaiters;
volatile uint64_t gSched;
IOLock *gStampLock;
uint32_t gStampPolls;

uint64_t wrapEvWaitForStamp(uint64_t self, uint64_t idx, uint64_t stamp, uint64_t out, uint64_t e, uint64_t f) {
	auto hw = *reinterpret_cast<uint64_t *>(self + 0xd38);
	if (hw && !gSched) {
		// As the original does: hw->getScheduler() is vtable + 0x3b0.
		auto vt = *reinterpret_cast<uint64_t **>(hw);
		gSched = reinterpret_cast<uint64_t (*)(uint64_t)>(vt[0x3b0 / 8])(hw);
		LOG("stamppoll: scheduler %llx", gSched);
	}
	if (OSIncrementAtomic(&gStampWaiters) == 0 && gStampLock) {
		IOLockLock(gStampLock);
		IOLockWakeup(gStampLock, &gStampLock, true);
		IOLockUnlock(gStampLock);
	}
	auto r = reinterpret_cast<Fn6>(orgEvWaitForStamp)(self, idx, stamp, out, e, f);
	OSDecrementAtomic(&gStampWaiters);
	return r;
}

void stampPoll(void *, wait_result_t) {
	for (;;) {
		IOLockLock(gStampLock);
		while (gStampWaiters <= 0)
			IOLockSleep(gStampLock, &gStampLock, THREAD_UNINT);
		IOLockUnlock(gStampLock);
		IOSleep(1);
		if (gStampWaiters > 0 && gSched && gCheckTimestamps) {
			reinterpret_cast<uint64_t (*)(uint64_t)>(gCheckTimestamps)(gSched);
			if (gStampPolls++ == 0)
				LOG("stamppoll: first poll");
		}
	}
}
}  // namespace

void vcnRouteAccel(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	if (!vcnEnabled())
		return;
	KernelPatcher::RouteRequest reqs[] {
		{"__ZN29AMDRadeonX6000_AMDMMHub_2_0_015fillVMRegistersEP25AMD_GFX10_VM_REGS_PER_HUB", wrapFillVm<0>, orgFillVm[0]},
		{"__ZN29AMDRadeonX6000_AMDMMHub_2_0_215fillVMRegistersEP25AMD_GFX10_VM_REGS_PER_HUB", wrapFillVm<1>, orgFillVm[1]},
		{"__ZN29AMDRadeonX6000_AMDMMHub_2_1_015fillVMRegistersEP25AMD_GFX10_VM_REGS_PER_HUB", wrapFillVm<2>, orgFillVm[2]},
		{"__ZN29AMDRadeonX6000_AMDMMHub_2_1_115fillVMRegistersEP25AMD_GFX10_VM_REGS_PER_HUB", wrapFillVm<3>, orgFillVm[3]},
	};
	for (auto &r : reqs) {
		if (!patcher.routeMultiple(index, &r, 1, address, size))
			LOG("mmfill: failed to route %s (err %d)", r.symbol, patcher.getError());
		patcher.clearError();
	}
	LOG("mmfill: hooks installed");
	wakeCrumbReport();
	if (checkKernelArgument("-rmbnostamppoll"))
		return;
	gCheckTimestamps = patcher.solveSymbol(index, "__ZN29AMDRadeonX6000_AMDSWScheduler15checkTimestampsEv", address, size);
	patcher.clearError();
	KernelPatcher::RouteRequest wait {"__ZN35AMDRadeonX6000_AMDAccelEventMachine12waitForStampEijPj", wrapEvWaitForStamp, orgEvWaitForStamp};
	gStampLock = IOLockAlloc();
	thread_t t;
	if (!gCheckTimestamps || !gStampLock) {
		LOG("stamppoll: checkTimestamps not found");
	} else if (!patcher.routeMultiple(index, &wait, 1, address, size)) {
		LOG("stamppoll: failed to route waitForStamp (err %d)", patcher.getError());
		patcher.clearError();
	} else if (kernel_thread_start(stampPoll, nullptr, &t) == KERN_SUCCESS) {
		thread_deallocate(t);
		LOG("stamppoll: installed");
	}
}

bool vcnArmMode() {
	static int on = -1;
	if (on < 0)
		on = checkKernelArgument("-rmbvcnarm") ? 1 : 0;
	return on;
}

// True once when /Users/user/m8/vcn-once is non-empty; empties it.
bool vcnArmFileConsume() {
	constexpr const char *Path = "/Users/user/m8/vcn-once";
	auto ctx = vfs_context_create(nullptr);
	vnode_t vp = nullptr;
	bool armed = false;
	if (!vnode_lookup(Path, 0, &vp, ctx)) {
		struct vnode_attr va;
		VATTR_INIT(&va);
		VATTR_WANTED(&va, va_data_size);
		armed = !vnode_getattr(vp, &va, ctx) && va.va_data_size > 0;
		vnode_put(vp);
		if (armed && !vnode_open(Path, FWRITE, 0644, 0, &vp, ctx)) {
			VATTR_INIT(&va);
			VATTR_SET(&va, va_data_size, 0);
			int e = vnode_setattr(vp, &va, ctx);
			vnode_close(vp, FWRITE, ctx);
			LOG("novcn: %s emptied (%d)", Path, e);
		}
	}
	vfs_context_rele(ctx);
	return armed;
}

bool vcnEnabled() {
	static int on = -1;
	if (on < 0)
		on = checkKernelArgument("-rmbvcn") || vcnArmMode() ? 1 : 0;
	return on;
}

void vcnRoute(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	if (!vcnEnabled())
		return;
	remapMmhubTables(address, size);
	KernelPatcher::RouteRequest reqs[] {
		{"__ZN11AMDFirmware14createFirmwareEPhjjPKc", wrapCreateFw, orgCreateFw},
		{"_engine_3_0_decode_sram_secure_initialize", wrap_engine_3_0_decode_sram_secure_initialize,
		 org_engine_3_0_decode_sram_secure_initialize},
		{"_queue_decode_3_0_build_command", wrap_queue_decode_3_0_build_command, org_queue_decode_3_0_build_command},
		{"_mmhub_2_3_0_initialize_registers", wrap_mmhub_2_3_0_initialize_registers, org_mmhub_2_3_0_initialize_registers},
	};
	for (auto &r : reqs) {
		if (!patcher.routeMultiple(index, &r, 1, address, size))
			LOG("vcn: failed to route %s (err %d)", r.symbol, patcher.getError());
		patcher.clearError();
	}
	LOG("vcn: hooks installed");
	if (checkKernelArgument("-rmbvcntrace"))
		vcnTraceRoute(patcher, index, address, size);
}
