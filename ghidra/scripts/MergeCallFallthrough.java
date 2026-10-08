// Merge function fragments that start exactly at the fall-through of a CALL
// in another function.  Such fragments appear when a callee was (wrongly)
// marked non-returning: the caller's body stopped at the call and the gap
// finder later turned the tail into its own function.  In Watcom output a
// real function never starts right after a call (there is always a ret and
// padding), so inside the game region this is safe.
//   -postScript MergeCallFallthrough.java [regionStart] [regionEnd]   default 0x10000 0x5e000
//@category MagicCarpet
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import java.util.ArrayList;
import java.util.List;

public class MergeCallFallthrough extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        long lo = a.length > 0 ? Long.decode(a[0]) : 0x10000L;
        long hi = a.length > 1 ? Long.decode(a[1]) : 0x5e000L;
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        int removedTotal = 0, pass = 0;
        while (true) {
            pass++;
            List<Address> fragments = new ArrayList<>();
            for (Function f : fm.getFunctions(true)) {
                long e = f.getEntryPoint().getOffset();
                if (e < lo || e >= hi) continue;
                for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
                    if (!ins.getFlowType().isCall()) continue;
                    Address ft = ins.getFallThrough();
                    if (ft == null) ft = ins.getMaxAddress().add(1);
                    Function g = fm.getFunctionAt(ft);
                    if (g == null || g == f) continue;
                    if (listing.getInstructionAt(ft) == null) continue;
                    fragments.add(ft);
                }
            }
            if (fragments.isEmpty()) break;
            for (Address ft : fragments) {
                Function g = fm.getFunctionAt(ft);
                if (g == null) continue;
                println("pass " + pass + ": merging fragment " + g.getName() + " into caller flow");
                fm.removeFunction(ft);
                removedTotal++;
            }
            // recompute bodies so callers absorb the freed instructions
            List<Function> all = new ArrayList<>();
            for (Function f : fm.getFunctions(true)) all.add(f);
            for (Function f : all) {
                long e = f.getEntryPoint().getOffset();
                if (e < lo || e >= hi) continue;
                try { CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor); } catch (Exception ex) { println("fixup failed " + f.getName() + ": " + ex.getMessage()); }
            }
            if (pass > 10) { println("giving up after 10 passes"); break; }
        }
        println("MergeCallFallthrough: removed " + removedTotal + " fragment functions in " + pass + " passes");
    }
}
