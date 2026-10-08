// Remove "functions" that are really switch-case bodies or other jump
// targets inside another function: entry has only JUMP references coming
// from a different function in the region, no CALL references and no data
// (handler-table) references.  Bodies of the remaining functions are then
// recomputed so they absorb the freed code.
//   -postScript MergeJumpFragments.java [regionStart] [regionEnd]   default 0x10000 0x5e000
//@category MagicCarpet
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import java.util.ArrayList;
import java.util.List;

public class MergeJumpFragments extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        long lo = a.length > 0 ? Long.decode(a[0]) : 0x10000L;
        long hi = a.length > 1 ? Long.decode(a[1]) : 0x5e000L;
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        int removed = 0;
        for (int pass = 0; pass < 5; pass++) {
            List<Address> victims = new ArrayList<>();
            for (Function f : fm.getFunctions(true)) {
                Address e = f.getEntryPoint();
                if (e.getOffset() < lo || e.getOffset() >= hi) continue;
                boolean jumpFromOther = false, callOrData = false;
                for (Reference r : rm.getReferencesTo(e)) {
                    if (r.getReferenceType().isCall()) { callOrData = true; break; }
                    // data refs from the data object are handler-table entries (protect);
                    // data refs from inside the code object are switch jump-table slots (do not)
                    if (r.getReferenceType().isData() && r.getFromAddress().getOffset() >= 0x90000L) { callOrData = true; break; }
                    if (r.getReferenceType().isJump()) {
                        Function from = fm.getFunctionContaining(r.getFromAddress());
                        if (from != null && from != f) jumpFromOther = true;
                    }
                }
                if (jumpFromOther && !callOrData && f.getSymbol().getSource() != ghidra.program.model.symbol.SourceType.USER_DEFINED) victims.add(e);
            }
            if (victims.isEmpty()) break;
            for (Address e : victims) {
                Function f = fm.getFunctionAt(e);
                if (f == null) continue;
                println("pass " + pass + ": removing jump fragment " + f.getName() + " (" + f.getBody().getNumAddresses() + " bytes)");
                fm.removeFunction(e);
                removed++;
            }
            List<Function> all = new ArrayList<>();
            for (Function f : fm.getFunctions(true)) all.add(f);
            for (Function f : all) {
                long e = f.getEntryPoint().getOffset();
                if (e < lo || e >= hi) continue;
                try { CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor); } catch (Exception ex) { println("fixup failed " + f.getName() + ": " + ex.getMessage()); }
            }
        }
        println("MergeJumpFragments: removed " + removed);
    }
}
