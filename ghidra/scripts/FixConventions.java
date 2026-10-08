// Give every function with an "unknown" calling convention the program's
// Watcom register convention (__watcall).  Functions created by the repair
// scripts (RepairFunctions, FixPointerTargets, the gap finder) end up with the
// unknown convention; the decompiler then cannot model their register
// arguments (EAX, EDX, EBX, ECX) and callers get "extrapop unknown" stack
// tracking.  163 functions were affected in carpet.exe.
//   -postScript FixConventions.java [convention]    default __watcall
//@category MagicCarpet
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class FixConventions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String cc = getScriptArgs().length > 0 ? getScriptArgs()[0] : "__watcall";
        int fixed = 0, total = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            total++;
            String name = f.getCallingConventionName();
            if (name == null || name.equals(Function.UNKNOWN_CALLING_CONVENTION_STRING)) {
                f.setCallingConvention(cc);
                fixed++;
            }
        }
        println("FixConventions: set " + cc + " on " + fixed + " of " + total + " functions");
    }
}
