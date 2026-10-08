// Apply function names / comments from a CSV so naming lives in version
// control and can be re-applied to a fresh Ghidra project.
//
//   CSV columns: address,name,comment   (header row required; '#' lines ignored)
//   address is hex (with or without 0x); name may be empty to only set a comment.
//   With a second argument `labels` the rows name code *inside* functions
//   (jump-table targets etc.): a label is created instead of a function.
//
// Usage (headless):
//   analyzeHeadless <proj> MC1 -process carpet.exe -noanalysis
//       -scriptPath <dir> -postScript ApplyNames.java <names.csv>
//@category MagicCarpet
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.FileReader;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("usage: ApplyNames.java <names.csv>");
            return;
        }
        FunctionManager fm = currentProgram.getFunctionManager();
        boolean labels = args.length > 1 && args[1].equals("labels");
        int applied = 0, created = 0, missing = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(args[0]))) {
            String line = r.readLine(); // header
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                String[] parts = line.split(",", 3);
                String addrStr = parts[0].trim().replaceFirst("^0x", "");
                String name = parts.length > 1 ? parts[1].trim() : "";
                String comment = parts.length > 2 ? parts[2].trim() : "";
                Address addr = toAddr(Long.parseLong(addrStr, 16));
                if (labels) {
                    if (!name.isEmpty()) createLabel(addr, name, true, SourceType.USER_DEFINED);
                    if (!comment.isEmpty()) currentProgram.getListing().setComment(addr, CodeUnit.PRE_COMMENT, comment);
                    applied++;
                    continue;
                }
                Function f = fm.getFunctionAt(addr);
                if (f == null) {
                    f = createFunction(addr, name.isEmpty() ? null : name);
                    if (f == null) {
                        printerr("no function at " + addr + " (" + name + ")");
                        missing++;
                        continue;
                    }
                    created++;
                }
                if (!name.isEmpty() && !f.getName().equals(name)) {
                    f.setName(name, SourceType.USER_DEFINED);
                }
                if (!comment.isEmpty()) {
                    currentProgram.getListing().setComment(addr, CodeUnit.PLATE_COMMENT, comment);
                }
                applied++;
            }
        }
        println("ApplyNames: applied=" + applied + " created=" + created + " missing=" + missing);
    }
}
