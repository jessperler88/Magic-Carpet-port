// Remove bogus "functions" (padding runs, tail fragments, functions started on
// padding bytes) listed in a text file, then recompute every function body in
// the region so neighbours re-absorb freed code that they flow into.
// Instructions are kept; only the function objects go.
//   -postScript DeleteStubs.java <list.txt> [regionStart] [regionEnd]
//   list: one hex address per line, '#' comments.  default region 0x10000 0x7c000
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

public class DeleteStubs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        List<String> lines = Files.readAllLines(Paths.get(a[0]));
        long lo = a.length > 1 ? Long.decode(a[1]) : 0x10000L;
        long hi = a.length > 2 ? Long.decode(a[2]) : 0x7c000L;
        FunctionManager fm = currentProgram.getFunctionManager();
        int removed = 0, missing = 0;
        for (String line : lines) {
            line = line.split("#")[0].trim();
            if (line.isEmpty()) continue;
            Address t = toAddr(Long.parseLong(line.replace("0x", ""), 16));
            Function f = fm.getFunctionAt(t);
            if (f == null) { missing++; continue; }
            fm.removeFunction(t);
            removed++;
        }
        println("DeleteStubs: removed=" + removed + " not-a-function=" + missing);
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
