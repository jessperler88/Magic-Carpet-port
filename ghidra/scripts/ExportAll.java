// Export everything useful from an analysed Magic Carpet executable:
//   <name>_functions.csv  address,name,size,callers,callees,convention
//   <name>_calls.csv      caller,callee  (static call graph edges)
//   <name>_strings.csv    address,length,xrefcount,xrefs,string
//   <name>_all.c          decompiled C for every function (grep-able)
// Usage (headless):
//   analyzeHeadless <proj> MC1 -process carpet.exe -noanalysis
//       -scriptPath <dir> -postScript ExportAll.java <outDir>
//@category MagicCarpet
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.Array;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.Pointer;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.TypeDef;
import ghidra.program.model.data.Undefined4DataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.listing.DataIterator;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

public class ExportAll extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File outDir = new File(args.length > 0 ? args[0] : "export");
        outDir.mkdirs();
        String base = currentProgram.getName().replaceAll("\\.exe$", "");
        FunctionManager fm = currentProgram.getFunctionManager();

        int total = fm.getFunctionCount();
        println("Exporting " + base + ": " + total + " functions");

        try (PrintWriter w = new PrintWriter(new File(outDir, base + "_functions.csv"));
             PrintWriter cg = new PrintWriter(new File(outDir, base + "_calls.csv"))) {
            w.println("address,name,size,callers,callees,convention");
            cg.println("caller,callee");
            for (Function f : fm.getFunctions(true)) {
                long size = f.getBody().getNumAddresses();
                Set<Function> callers = f.getCallingFunctions(monitor);
                Set<Function> callees = f.getCalledFunctions(monitor);
                w.printf("%s,%s,%d,%d,%d,%s%n", f.getEntryPoint(), f.getName(), size,
                        callers.size(), callees.size(), f.getCallingConventionName());
                for (Function c : callees) {
                    cg.printf("%s,%s%n", f.getEntryPoint(), c.getEntryPoint());
                }
            }
        }

        try (PrintWriter w = new PrintWriter(new File(outDir, base + "_strings.csv"))) {
            w.println("address,length,xrefcount,xrefs,string");
            DataIterator di = currentProgram.getListing().getDefinedData(true);
            while (di.hasNext()) {
                writeStrings(w, di.next(), 0);
            }
        }

        DecompInterface ifc = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        ifc.setOptions(opts);
        ifc.toggleCCode(true);
        ifc.toggleSyntaxTree(false);
        ifc.setSimplificationStyle("decompile");
        if (!ifc.openProgram(currentProgram)) {
            printerr("decompiler failed to open program: " + ifc.getLastMessage());
            return;
        }
        // Globals typed as pointer-to-struct (g_state, g_cfg, g_work ...).  The decompiler hangs on
        // game_main_32a00 while resolving g_state->players[p] (a struct of size 0x801 at +0x340b,
        // Ghidra 12.0.4); as a fallback such functions are retried with these globals untyped.
        List<Data> structPtrGlobals = new ArrayList<>();
        for (Data d : currentProgram.getListing().getDefinedData(true)) {
            DataType dt = d.getDataType();
            if (dt instanceof TypeDef) dt = ((TypeDef) dt).getBaseDataType();
            if (dt instanceof Pointer && ((Pointer) dt).getDataType() instanceof Structure) structPtrGlobals.add(d);
        }
        int n = 0, fail = 0, fallback = 0;
        try (PrintWriter all = new PrintWriter(new File(outDir, base + "_all.c"))) {
            for (Function f : fm.getFunctions(true)) {
                if (monitor.isCancelled()) break;
                DecompileResults res = ifc.decompileFunction(f, 120, monitor);
                boolean ok = res != null && res.decompileCompleted() && res.getDecompiledFunction() != null;
                String note = "";
                if (!ok) {
                    println("  retrying " + f.getName() + " with struct-pointer globals untyped: " + (res == null ? "null" : res.getErrorMessage()));
                    res = decompileWithUntypedGlobals(f, structPtrGlobals);
                    ok = res != null && res.decompileCompleted() && res.getDecompiledFunction() != null;
                    if (ok) { fallback++; note = "  (FALLBACK: decompiled with struct-pointer globals untyped)"; }
                    if (!ifc.openProgram(currentProgram)) printerr("decompiler re-open failed: " + ifc.getLastMessage());
                }
                if (ok) {
                    all.println("// ==== " + f.getEntryPoint() + " " + f.getName() + " size=" + f.getBody().getNumAddresses() + note);
                    all.println(res.getDecompiledFunction().getC());
                    n++;
                } else {
                    fail++;
                    all.println("// ==== FAILED " + f.getEntryPoint() + " " + f.getName() + " : "
                            + (res == null ? "null" : res.getErrorMessage()));
                }
                if ((n + fail) % 250 == 0) println("  decompiled " + (n + fail) + "/" + total);
            }
        } finally {
            ifc.dispose();
        }
        println("Done " + base + ": decompiled=" + n + " failed=" + fail + " via-fallback=" + fallback);
    }

    /** One strings.csv row per string-valued data item. Arrays of a non-char byte type (u8 / i8 tables typed by
     *  ApplyTypes) are skipped; structures and arrays of structures are searched for char[] / string fields
     *  (round 6: the ResourceRec lists keep their file names in the CSV). */
    private void writeStrings(PrintWriter w, Data d, int depth) {
        DataType dt = d.getDataType();
        if (dt instanceof TypeDef) dt = ((TypeDef) dt).getBaseDataType();
        if (dt instanceof Structure || (dt instanceof Array && !d.hasStringValue())) {
            if (depth > 3) return;
            // only the program's own data (not the .image overlay with the MZ / LE headers)
            if (!d.getAddress().getAddressSpace().equals(currentProgram.getAddressFactory().getDefaultAddressSpace())) return;
            int n = d.getNumComponents();
            for (int i = 0; i < n; i++) {
                Data c = d.getComponent(i);
                if (c == null) continue;
                DataType ct = c.getDataType();
                if (ct instanceof TypeDef) ct = ((TypeDef) ct).getBaseDataType();
                if (!(ct instanceof Structure || ct instanceof Array)) {
                    if (!(dt instanceof Structure)) return;   // array of scalars: nothing inside
                    continue;
                }
                writeStrings(w, c, depth + 1);
            }
            return;
        }
        if (!d.hasStringValue()) return;
        if (dt instanceof Array) {
            DataType el = ((Array) dt).getDataType();
            if (el instanceof TypeDef) el = ((TypeDef) el).getBaseDataType();
            if (!el.getName().equals("char")) return;          // u8 / i8 tables are not strings
        }
        Object v = d.getValue();
        String s = v == null ? "" : v.toString();
        if (depth > 0 && s.isEmpty()) return;
        s = s.replace("\r", "\\r").replace("\n", "\\n").replace("\"", "\"\"");
        ReferenceIterator it = d.getReferenceIteratorTo();
        int n = 0;
        StringBuilder xr = new StringBuilder();
        while (it.hasNext()) {
            Reference r = it.next();
            if (n < 8) {
                if (n > 0) xr.append(' ');
                xr.append(r.getFromAddress());
            }
            n++;
        }
        w.printf("%s,%d,%d,\"%s\",\"%s\"%n", d.getAddress(), d.getLength(), n, xr, s);
    }

    /** Retry one function with every pointer-to-struct global temporarily untyped, in a fresh decompiler
     *  process (the decompiler caches types per process).  The original types are restored afterwards. */
    private DecompileResults decompileWithUntypedGlobals(Function f, List<Data> globals) {
        Listing listing = currentProgram.getListing();
        List<Object[]> saved = new ArrayList<>();
        for (Data d : globals) {
            Address a = d.getAddress();
            saved.add(new Object[] { a, d.getDataType() });
            listing.clearCodeUnits(a, a.add(d.getLength() - 1), false);
            try { listing.createData(a, Undefined4DataType.dataType); } catch (Exception ex) { printerr("untype " + a + ": " + ex.getMessage()); }
        }
        DecompInterface tmp = new DecompInterface();
        DecompileResults res = null;
        try {
            tmp.setOptions(new DecompileOptions());
            tmp.toggleCCode(true);
            tmp.toggleSyntaxTree(false);
            tmp.setSimplificationStyle("decompile");
            if (tmp.openProgram(currentProgram)) res = tmp.decompileFunction(f, 120, monitor);
            else printerr("fallback decompiler failed to open program: " + tmp.getLastMessage());
        } finally {
            tmp.dispose();
            for (Object[] s : saved) {
                Address a = (Address) s[0];
                DataType dt = (DataType) s[1];
                listing.clearCodeUnits(a, a.add(dt.getLength() - 1), false);
                try { listing.createData(a, dt); } catch (Exception ex) { printerr("retype " + a + ": " + ex.getMessage()); }
            }
        }
        return res;
    }
}
