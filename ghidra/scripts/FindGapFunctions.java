// Bounded replacement for Ghidra's "Aggressive Instruction Finder" (which spun
// for 30+ minutes on carpet.exe).  Walks the undefined gaps inside the code
// object, and at each candidate start (first byte after a return/padding run,
// or any 16-byte aligned address) tries to disassemble.  A candidate becomes a
// function if the flow from it reaches a RET within `maxInstr` instructions
// without hitting an already-defined instruction mid-way, an invalid opcode,
// or a jump out of the gap.
//
//   -postScript FindGapFunctions.java [codeStart] [codeEnd] [maxInstr]
//   defaults: 0x10000 0x5e000 4000
//@category MagicCarpet
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.disassemble.Disassembler;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.lang.InstructionBlock;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.FlowType;

import java.util.ArrayList;
import java.util.List;

public class FindGapFunctions extends GhidraScript {

    /** Length of a Watcom/MASM padding sequence starting at p, or 0. */
    private static int padLength(Memory mem, Address p, Address limit) {
        try {
            if (p.compareTo(limit) > 0) return 0;
            int b0 = mem.getByte(p) & 0xFF;
            if (b0 == 0xCC || b0 == 0x90) return 1;
            if (b0 == 0x8B && (mem.getByte(p.add(1)) & 0xFF) == 0xFF) return 2;           // mov edi,edi
            if (b0 == 0x8D) {
                int b1 = mem.getByte(p.add(1)) & 0xFF;
                if ((b1 == 0x40 || b1 == 0x49 || b1 == 0x52 || b1 == 0x5B || b1 == 0x76 || b1 == 0x7F) && mem.getByte(p.add(2)) == 0) return 3;
                if (b1 == 0x64 && (mem.getByte(p.add(2)) & 0xFF) == 0x24 && mem.getByte(p.add(3)) == 0) return 4;
                if ((b1 == 0x80 || b1 == 0x89 || b1 == 0x92 || b1 == 0x9B || b1 == 0xB6 || b1 == 0xBF)
                        && mem.getByte(p.add(2)) == 0 && mem.getByte(p.add(3)) == 0 && mem.getByte(p.add(4)) == 0 && mem.getByte(p.add(5)) == 0) return 6;
            }
        } catch (Exception ex) { return 0; }
        return 0;
    }

    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        long codeStart = a.length > 0 ? Long.decode(a[0]) : 0x10000L;
        long codeEnd = a.length > 1 ? Long.decode(a[1]) : 0x5e000L;
        int maxInstr = a.length > 2 ? Integer.parseInt(a[2]) : 4000;

        Listing listing = currentProgram.getListing();
        Memory mem = currentProgram.getMemory();
        AddressSet region = new AddressSet(toAddr(codeStart), toAddr(codeEnd - 1));
        AddressSetView undefined = listing.getUndefinedRanges(region, false, monitor);
        println("undefined bytes in region: " + undefined.getNumAddresses());

        List<Address> candidates = new ArrayList<>();
        java.util.Set<Long> seen = new java.util.HashSet<>();
        for (ghidra.program.model.address.AddressRange r : undefined) {
            Address s = r.getMinAddress();
            Address e = r.getMaxAddress();
            if (e.subtract(s) < 4) continue;
            // every address that follows a padding run or a return inside the gap
            Address p = s;
            boolean afterPad = true;  // the gap start itself (follows defined code) is a candidate
            while (p.compareTo(e) < 0) {
                int pad = padLength(mem, p, e);
                if (pad > 0) { p = p.add(pad); afterPad = true; continue; }
                if (afterPad && seen.add(p.getOffset())) candidates.add(p);
                afterPad = false;
                int b = mem.getByte(p) & 0xFF;
                if (b == 0xC3) { afterPad = true; p = p.add(1); continue; }
                if (b == 0xC2) { afterPad = true; p = p.add(3); continue; }
                p = p.add(1);
            }
        }
        // code pointers stored in data (switch/function tables) anywhere in memory
        int ptrHits = 0;
        for (ghidra.program.model.mem.MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized()) continue;
            Address a0 = blk.getStart();
            long n = blk.getSize();
            byte[] buf = new byte[(int) n];
            mem.getBytes(a0, buf);
            for (int i = 0; i + 4 <= n; i += 2) {
                long v = (buf[i] & 0xFFL) | ((buf[i + 1] & 0xFFL) << 8) | ((buf[i + 2] & 0xFFL) << 16) | ((buf[i + 3] & 0xFFL) << 24);
                if (v < codeStart || v >= codeEnd) continue;
                Address t = toAddr(v);
                if (listing.getInstructionAt(t) != null || listing.getFunctionContaining(t) != null) continue;
                if (listing.getDefinedDataContaining(t) != null) continue;
                if (!undefined.contains(t)) continue;
                if (v <= codeStart) continue;
                int prev;
                try { prev = mem.getByte(t.subtract(1)) & 0xFF; } catch (Exception ex) { continue; }
                boolean ok = prev == 0xC3 || prev == 0xCC || prev == 0x90 || prev == 0x00 || padLength(mem, t.subtract(6), t) == 6
                        || padLength(mem, t.subtract(3), t) == 3 || padLength(mem, t.subtract(4), t) == 4 || padLength(mem, t.subtract(2), t) == 2;
                if (!ok && (i % 4) == 0) {
                    // jump-table heuristic: this dword sits in a run of >= 3 consecutive in-range code pointers
                    int run = 0;
                    for (int k = i - 8; k <= i + 8; k += 4) {
                        if (k < 0 || k + 4 > n) continue;
                        long w = (buf[k] & 0xFFL) | ((buf[k + 1] & 0xFFL) << 8) | ((buf[k + 2] & 0xFFL) << 16) | ((buf[k + 3] & 0xFFL) << 24);
                        if (w >= codeStart && w < codeEnd) run++;
                    }
                    ok = run >= 3;
                }
                if (ok && seen.add(v)) { candidates.add(t); ptrHits++; }
            }
        }
        println("pointer-table candidates: " + ptrHits);
        println("candidate starts: " + candidates.size());

        Disassembler dis = Disassembler.getDisassembler(currentProgram, monitor, null);
        int created = 0, tried = 0;
        for (Address c : candidates) {
            if (monitor.isCancelled()) break;
            if (listing.getInstructionAt(c) != null || listing.getDefinedDataAt(c) != null) continue;
            if (listing.getFunctionContaining(c) != null) continue;
            tried++;
            // Dry-run disassembly to validate: pseudo-disassemble up to maxInstr following flow.
            // pseudoDisassembleBlock only decodes ONE basic block, so do not demand a RET;
            // accept a clean block of >= 3 instructions whose flow stays inside the region.
            InstructionBlock blk = dis.pseudoDisassembleBlock(c, null, maxInstr);
            if (blk == null || blk.hasInstructionError()) continue;
            boolean bad = false;
            int count = 0;
            Instruction last = null;
            for (Instruction ins : blk) {
                count++;
                last = ins;
                Address ia = ins.getAddress();
                if (ia.getOffset() < codeStart || ia.getOffset() >= codeEnd) { bad = true; break; }
                if (listing.getDefinedDataContaining(ia) != null) { bad = true; break; }
            }
            if (bad || count < 3 || last == null) continue;
            FlowType ft = last.getFlowType();
            if (!(ft.isTerminal() || ft.isJump() || ft.isCall() || ft.isConditional())) {
                // fell off the end of the block without a control-flow instruction: decoder gave up
                continue;
            }
            for (Address ref : last.getFlows()) {
                if (ref.getOffset() < codeStart || ref.getOffset() >= codeEnd) { bad = true; }
            }
            if (bad) continue;
            DisassembleCommand dc = new DisassembleCommand(c, null, true);
            dc.applyTo(currentProgram, monitor);
            CreateFunctionCmd fc = new CreateFunctionCmd(c);
            if (fc.applyTo(currentProgram, monitor)) created++;
        }
        println("FindGapFunctions: tried=" + tried + " created=" + created);
        if (created > 0) {
            println("re-running auto-analysis for new functions...");
            analyzeAll(currentProgram);
        }
    }
}
