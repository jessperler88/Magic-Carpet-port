// Debug helper: print the code units (instructions / data / undefined) around
// each address given as an argument.   -postScript DumpUnits.java 0x44140 0x45350
//@category MagicCarpet
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;

public class DumpUnits extends GhidraScript {
    @Override
    protected void run() throws Exception {
        Listing l = currentProgram.getListing();
        for (String s : getScriptArgs()) {
            Address a = toAddr(Long.decode(s));
            println("==== around " + a);
            Address p = a.subtract(16);
            int n = 0;
            while (p.compareTo(a.add(48)) < 0 && n < 40) {
                CodeUnit cu = l.getCodeUnitAt(p);
                if (cu == null) { cu = l.getCodeUnitContaining(p); }
                if (cu == null) { println("  " + p + "  <none>"); p = p.add(1); n++; continue; }
                Function f = l.getFunctionContaining(cu.getAddress());
                String kind = (cu instanceof Data) ? (((Data) cu).isDefined() ? "DATA " + ((Data) cu).getDataType().getName() : "undef") : "INSN";
                println(String.format("  %s  %-28s %-6s len=%d  %s", cu.getAddress(), cu.toString(), kind, cu.getLength(), f == null ? "" : f.getName()));
                p = cu.getAddress().add(cu.getLength());
                n++;
            }
        }
    }
}
