// Restore the fall-through of CALL instructions whose callee was wrongly
// marked non-returning.  The no-return analyzer leaves a fall-through
// override (none) on every call site, so clearing the function flag alone
// does not let the caller's body grow again.  After clearing the overrides
// every function body in the region is recomputed.
//   -postScript RestoreCallFallthrough.java [regionStart] [regionEnd]   default 0x10000 0x7c000
//@category MagicCarpet
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import java.util.ArrayList;
import java.util.List;

public class RestoreCallFallthrough extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        long lo = a.length > 0 ? Long.decode(a[0]) : 0x10000L;
        long hi = a.length > 1 ? Long.decode(a[1]) : 0x7c000L;
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        AddressSet region = new AddressSet(toAddr(lo), toAddr(hi - 1));
        int restored = 0, keptNoReturn = 0, overridden = 0;
        for (Instruction ins : listing.getInstructions(region, true)) {
            if (!ins.getFlowType().isCall()) continue;
            if (ins.getFallThrough() != null) continue;
            // call with no fall-through: is the callee really no-return?
            boolean calleeNoReturn = false;
            for (Reference r : ins.getReferencesFrom()) {
                if (!r.getReferenceType().isCall()) continue;
                Function callee = fm.getFunctionAt(r.getToAddress());
                if (callee != null && callee.hasNoReturn()) calleeNoReturn = true;
            }
            if (calleeNoReturn) { keptNoReturn++; continue; }
            if (restored < 5) println("diag " + ins.getAddress() + " " + ins + " flowOverride=" + ins.getFlowOverride() + " ftOverridden=" + ins.isFallThroughOverridden() + " flowType=" + ins.getFlowType());
            if (ins.getFlowOverride() != ghidra.program.model.listing.FlowOverride.NONE) { ins.setFlowOverride(ghidra.program.model.listing.FlowOverride.NONE); overridden++; }
            if (ins.isFallThroughOverridden()) { ins.clearFallThroughOverride(); overridden++; }
            if (ins.getFallThrough() == null) ins.setFallThrough(ins.getMaxAddress().add(1));
            restored++;
            Address ft = ins.getMaxAddress().add(1);
            if (listing.getInstructionAt(ft) == null && listing.getDefinedDataAt(ft) == null) disassemble(ft);
        }
        println("RestoreCallFallthrough: restored=" + restored + " (overrides cleared " + overridden + "), kept genuine no-return calls=" + keptNoReturn);
        for (int pass = 0; pass < 3; pass++) {
            List<Function> all = new ArrayList<>();
            for (Function f : fm.getFunctions(true)) all.add(f);
            int grown = 0;
            for (Function f : all) {
                long e = f.getEntryPoint().getOffset();
                if (e < lo || e >= hi) continue;
                long before = f.getBody().getNumAddresses();
                try { CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor); } catch (Exception ex) { println("fixup failed " + f.getName() + ": " + ex.getMessage()); continue; }
                Function g = fm.getFunctionAt(f.getEntryPoint());
                if (g != null && g.getBody().getNumAddresses() != before) grown++;
            }
            println("pass " + pass + ": bodies changed=" + grown);
            if (grown == 0) break;
        }
    }
}
