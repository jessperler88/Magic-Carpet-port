// Undo false "non-returning function" discoveries.  Ghidra's
// "Non-Returning Functions - Discovered" analyzer misfires on Watcom
// stack-argument helpers (FUN_00035560 = thing allocator, FUN_00049720 =
// sound helper, ...), which truncates every caller's body at the call.
//   1. clear the no-return flag on each listed function,
//   2. switch the analyzer off for this program,
//   3. recompute the body of every function (CreateFunctionCmd.fixupFunctionBody).
//   -postScript ClearNoReturn.java <list.txt>   (one hex address per line; '#' comments)
//@category MagicCarpet
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

public class ClearNoReturn extends GhidraScript {
    @Override
    protected void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        int cleared = 0;
        if (getScriptArgs().length > 0) {
            for (String line : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
                line = line.split("#")[0].trim();
                if (line.isEmpty()) continue;
                Address a = toAddr(Long.parseLong(line.replace("0x", ""), 16));
                Function f = fm.getFunctionAt(a);
                if (f == null) { println("no function at " + a); continue; }
                if (f.hasNoReturn()) { f.setNoReturn(false); cleared++; }
            }
        }
        // any other discovered no-return function in the game region is suspect too: list them
        for (Function f : fm.getFunctions(true)) {
            if (f.hasNoReturn() && f.getEntryPoint().getOffset() < 0x5e000) println("still no-return: " + f.getName());
        }
        try { setAnalysisOption(currentProgram, "Non-Returning Functions - Discovered", "false"); }
        catch (Exception ex) { println("could not set analyzer option: " + ex.getMessage()); }
        println("cleared no-return on " + cleared + " functions; recomputing bodies");
        List<Function> all = new ArrayList<>();
        for (Function f : fm.getFunctions(true)) all.add(f);
        int grown = 0;
        for (Function f : all) {
            if (monitor.isCancelled()) break;
            long before = f.getBody().getNumAddresses();
            try { CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor); }
            catch (Exception ex) { println("fixup failed for " + f.getName() + ": " + ex.getMessage()); continue; }
            Function g = fm.getFunctionAt(f.getEntryPoint());
            if (g != null && g.getBody().getNumAddresses() > before) grown++;
        }
        println("ClearNoReturn: bodies grown=" + grown + " of " + all.size());
    }
}
