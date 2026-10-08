// Resolve indirect jumps through dword tables that the analyzer gave up on
// ("Could not recover jumptable ... Too many branches", flow treated as a
// call).  For each listed site: mark the table as pointers, add a
// COMPUTED_JUMP reference from the jump instruction to every distinct target,
// disassemble the targets, clear any flow override, then recompute the body
// of the enclosing function so it absorbs the targets.
//   -postScript FixJumpTables.java <list.txt>
//   list lines: <site>,<table>,<count>   (hex site/table, decimal count; '#' comments)
//@category MagicCarpet
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class FixJumpTables extends GhidraScript {
    @Override
    protected void run() throws Exception {
        List<String> lines = Files.readAllLines(Paths.get(getScriptArgs()[0]));
        Listing listing = currentProgram.getListing();
        Memory mem = currentProgram.getMemory();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Set<Function> touched = new LinkedHashSet<>();
        int sites = 0, refs = 0;
        for (String line : lines) {
            line = line.split("#")[0].trim();
            if (line.isEmpty()) continue;
            String[] p = line.split(",");
            Address site = toAddr(Long.parseLong(p[0].trim().replace("0x", ""), 16));
            Address table = toAddr(Long.parseLong(p[1].trim().replace("0x", ""), 16));
            int count = Integer.parseInt(p[2].trim());
            Instruction ins = listing.getInstructionAt(site);
            if (ins == null) {
                listing.clearCodeUnits(site, site, false);
                new DisassembleCommand(site, null, true).applyTo(currentProgram, monitor);
                ins = listing.getInstructionAt(site);
            }
            if (ins == null) { println("no instruction at " + site); continue; }
            // table entries as pointers
            Set<Address> targets = new LinkedHashSet<>();
            for (int i = 0; i < count; i++) {
                Address slot = table.add(4L * i);
                long v = mem.getInt(slot) & 0xffffffffL;
                targets.add(toAddr(v));
                if (listing.getInstructionContaining(slot) != null) listing.clearCodeUnits(slot, slot.add(3), false);
                if (listing.getDefinedDataAt(slot) == null) {
                    try { listing.createData(slot, PointerDataType.dataType); } catch (Exception ex) { /* already defined */ }
                }
            }
            // drop the bogus call/jump refs the analyzer left on the site, keep the table read
            for (Reference r : rm.getReferencesFrom(site)) {
                if (r.getReferenceType().isCall() || r.getReferenceType().isJump()) rm.delete(r);
            }
            for (Address t : targets) {
                rm.addMemoryReference(site, t, RefType.COMPUTED_JUMP, SourceType.USER_DEFINED, 0);
                refs++;
                if (listing.getInstructionAt(t) == null) {
                    if (listing.getDefinedDataAt(t) != null) listing.clearCodeUnits(t, t, false);
                    new DisassembleCommand(t, null, true).applyTo(currentProgram, monitor);
                }
            }
            if (ins.getFlowOverride() != FlowOverride.NONE) ins.setFlowOverride(FlowOverride.NONE);
            if (ins.isFallThroughOverridden()) ins.clearFallThroughOverride();
            Function f = listing.getFunctionContaining(site);
            if (f != null) touched.add(f);
            sites++;
        }
        for (Function f : touched) {
            long before = f.getBody().getNumAddresses();
            try { CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor); } catch (Exception ex) { println("fixup failed " + f.getName() + ": " + ex.getMessage()); }
            Function g = currentProgram.getFunctionManager().getFunctionAt(f.getEntryPoint());
            println("body of " + f.getName() + ": " + before + " -> " + (g == null ? -1 : g.getBody().getNumAddresses()) + " bytes");
        }
        println("FixJumpTables: sites=" + sites + " refs=" + refs);
    }
}
