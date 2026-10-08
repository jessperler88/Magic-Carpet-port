// List every function flagged no-return, with its caller count, so false
// positives of the "Non-Returning Functions" analyzers can be spotted.
//   -postScript ListNoReturn.java [regionStart] [regionEnd]   default 0x10000 0x7c000
//@category MagicCarpet
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class ListNoReturn extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        long lo = a.length > 0 ? Long.decode(a[0]) : 0x10000L;
        long hi = a.length > 1 ? Long.decode(a[1]) : 0x7c000L;
        int n = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            long e = f.getEntryPoint().getOffset();
            if (e < lo || e >= hi || !f.hasNoReturn()) continue;
            int calls = 0;
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint())) if (r.getReferenceType().isCall()) calls++;
            println("NORETURN " + f.getEntryPoint() + " " + f.getName() + " size=" + f.getBody().getNumAddresses() + " callers=" + calls);
            n++;
        }
        println("ListNoReturn: " + n + " functions flagged no-return");
    }
}
