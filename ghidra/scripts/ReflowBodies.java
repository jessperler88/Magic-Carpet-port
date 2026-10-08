// After bogus functions were removed (DeleteStubs), their mis-aligned
// instruction units still block the real owners from growing: FollowFlow
// stops at undefined/garbage bytes.  This script clears the code units in the
// listed former bodies (only where no function owns them now), re-disassembles
// following the flow from every function entry in the region, then recomputes
// all function bodies.
//   -postScript ReflowBodies.java <ranges.txt> [regionStart] [regionEnd] [force]
//   force: clear the ranges even where a function body owns them (the owners are re-fixed afterwards)
//   ranges: start,end hex per line ('#' comments)
//@category MagicCarpet
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Listing;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

public class ReflowBodies extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        List<String> lines = Files.readAllLines(Paths.get(a[0]));
        long lo = a.length > 1 ? Long.decode(a[1]) : 0x10000L;
        long hi = a.length > 2 ? Long.decode(a[2]) : 0x7c000L;
        boolean force = a.length > 3 && a[3].equals("force");
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        AddressSet owned = new AddressSet();
        for (Function f : fm.getFunctions(true)) owned.add(f.getBody());
        int cleared = 0;
        for (String line : lines) {
            line = line.split("#")[0].trim();
            if (line.isEmpty()) continue;
            String[] p = line.split(",");
            Address s = toAddr(Long.parseLong(p[0].trim().replace("0x", ""), 16));
            Address e = toAddr(Long.parseLong(p[1].trim().replace("0x", ""), 16) - 1);
            AddressSet rng = new AddressSet(s, e);
            if (!force) rng.delete(owned);
            for (ghidra.program.model.address.AddressRange r : rng) {
                listing.clearCodeUnits(r.getMinAddress(), r.getMaxAddress(), false);
                cleared += r.getLength();
            }
        }
        println("ReflowBodies: cleared " + cleared + " bytes of stale code units");
        // disassemble every flow target (jump / fall-through) that is still undefined, until nothing new appears
        AddressSet region = new AddressSet(toAddr(lo), toAddr(hi - 1));
        int dis = 0; int lastTodo = -1;
        for (int round = 0; round < 20; round++) {
            List<Address> todo = new ArrayList<>();
            for (ghidra.program.model.listing.Instruction ins : listing.getInstructions(region, true)) {
                Address[] flows = ins.getFlows();
                Address ft = ins.getFallThrough();
                List<Address> dests = new ArrayList<>();
                if (ft != null) dests.add(ft);
                for (Address d : flows) if (!ins.getFlowType().isCall()) dests.add(d);
                for (Address d : dests) {
                    if (!region.contains(d)) continue;
                    if (listing.getInstructionAt(d) != null || listing.getDefinedDataAt(d) != null) continue;
                    if (listing.getInstructionContaining(d) != null) continue; // mid-instruction: leave
                    todo.add(d);
                }
            }
            if (todo.isEmpty()) break;
            if (round > 0 && todo.size() == lastTodo) { println("stuck targets (first 12): " + todo.subList(0, Math.min(200, todo.size()))); break; }
            lastTodo = todo.size();
            int n = 0, rescued = 0;
            for (Address d : todo) {
                DisassembleCommand cmd = new DisassembleCommand(d, null, true);
                cmd.applyTo(currentProgram, monitor);
                if (listing.getInstructionAt(d) != null) { n++; continue; }
                // blocked by orphan mis-aligned instruction units: clear them (never inside a function body
                // or defined data) up to the next function entry, then retry
                if (listing.getDefinedDataContaining(d) != null) continue;
                ghidra.program.model.listing.FunctionIterator itF = fm.getFunctions(d.add(1), true);
                Function nextF = itF.hasNext() ? itF.next() : null;
                Address clearEnd = d.add(0x100);
                if (nextF != null && nextF.getEntryPoint().compareTo(clearEnd) < 0) clearEnd = nextF.getEntryPoint();
                AddressSet rng = new AddressSet(d, clearEnd.subtract(1));
                for (Function f : fm.getFunctions(true)) { if (f.getBody().intersects(rng)) rng.delete(f.getBody()); }
                for (ghidra.program.model.address.AddressRange r : rng) listing.clearCodeUnits(r.getMinAddress(), r.getMaxAddress(), false);
                new DisassembleCommand(d, null, true).applyTo(currentProgram, monitor);
                if (listing.getInstructionAt(d) != null) { n++; rescued++; }
            }
            if (rescued > 0) println("  rescued " + rescued + " targets by clearing orphan code units");
            println("round " + round + ": undefined flow targets=" + todo.size() + " disassembled=" + n);
            dis += n;
            if (n == 0) break;
        }
        println("ReflowBodies: disassembled from " + dis + " flow targets");
        for (int pass = 0; pass < 4; pass++) {
            List<Function> all = new ArrayList<>();
            for (Function f : fm.getFunctions(true)) all.add(f);
            int grown = 0;
            for (Function f : all) {
                long e = f.getEntryPoint().getOffset();
                if (e < lo || e >= hi) continue;
                long before = f.getBody().getNumAddresses();
                try { CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor); } catch (Exception ex) { println("fixup failed " + f.getName() + ": " + ex.getMessage()); continue; }
                Function g = fm.getFunctionAt(f.getEntryPoint());
                if (g != null && g.getBody().getNumAddresses() != before) { grown++; if (pass >= 2) println("  oscillating: " + f.getName() + " " + before + " -> " + g.getBody().getNumAddresses()); }
            }
            println("pass " + pass + ": bodies changed=" + grown);
            if (grown == 0) break;
        }
    }
}
