// Remove "functions" that sit on switch jump tables inside the code object and
// lay the tables down as pointer data.
//
// The analyser labels a recovered table `switchdataD_<addr>`; a later gap
// filler or repair pass turned nine of those tables in carpet.exe into
// functions (bodies of 4..43 bytes of pointer data disassembled as garbage).
// The script visits every symbol with the given prefix (function or plain
// label, so it can be rerun), finds the COMPUTED_JUMP site that references the
// table (a DATA reference from a `jmp [reg*4+table]` instruction), collects the
// site's computed-jump targets and counts table slots while the dwords are one
// of those targets (duplicates allowed, so the slot count is exact).  Without a
// site it counts consecutive dwords that point into the code object.  Then it
// deletes the function (if any), clears the code units over the table and
// creates pointer data for every slot, keeping the label.
//   -postScript RemoveTableFunctions.java [prefix]   default switchdataD_
//@category MagicCarpet
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class RemoveTableFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String prefix = getScriptArgs().length > 0 ? getScriptArgs()[0] : "switchdataD_";
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Memory mem = currentProgram.getMemory();
        long codeLo = 0x10000L, codeHi = 0x90000L;
        List<Address> tables = new ArrayList<>();
        SymbolIterator it = currentProgram.getSymbolTable().getSymbolIterator(prefix + "*", true);
        while (it.hasNext()) {
            Symbol s = it.next();
            if (s.getAddress().getOffset() >= codeLo && s.getAddress().getOffset() < codeHi && !tables.contains(s.getAddress())) tables.add(s.getAddress());
        }
        int removedFunctions = 0, laid = 0;
        for (Address table : tables) {
            // computed-jump targets of the site(s) referencing this table
            Set<Long> targets = new HashSet<>();
            for (Reference r : rm.getReferencesTo(table)) {
                if (!r.getReferenceType().isData()) continue;
                Address site = r.getFromAddress();
                if (listing.getInstructionAt(site) == null) continue;
                for (Reference rr : rm.getReferencesFrom(site)) {
                    if (rr.getReferenceType().isComputed() && rr.getReferenceType().isJump()) targets.add(rr.getToAddress().getOffset());
                }
            }
            int count = 0;
            Address a = table;
            while (count < 256) {
                long v = mem.getInt(a) & 0xffffffffL;
                if (targets.isEmpty()) {
                    if (v < codeLo || v >= codeHi) break;
                    if (count > 0 && (fm.getFunctionAt(a) != null || currentProgram.getSymbolTable().getPrimarySymbol(a) != null)) break;
                } else {
                    if (!targets.contains(v)) break;
                    if (count > 0 && (fm.getFunctionAt(a) != null || currentProgram.getSymbolTable().getPrimarySymbol(a) != null)) break;   // adjacent table
                }
                count++; a = a.add(4);
            }
            if (count == 0) { println("no table size for " + table + "; skipped"); continue; }
            String label = currentProgram.getSymbolTable().getPrimarySymbol(table) != null ? currentProgram.getSymbolTable().getPrimarySymbol(table).getName() : prefix + table;
            Function f = fm.getFunctionAt(table);
            if (f != null) { fm.removeFunction(table); removedFunctions++; }
            Address end = table.add(count * 4L - 1);
            boolean complete = true;
            for (int i = 0; i < count && complete; i++) {
                Data d = listing.getDefinedDataAt(table.add(i * 4L));
                if (d == null || !(d.getDataType() instanceof ghidra.program.model.data.Pointer)) complete = false;
            }
            if (!complete) {
                listing.clearCodeUnits(table, end, false);
                for (int i = 0; i < count; i++) {
                    Address slot = table.add(i * 4L);
                    try { listing.createData(slot, PointerDataType.dataType); } catch (Exception ex) { println("pointer at " + slot + ": " + ex.getMessage()); }
                }
                laid++;
            }
            if (currentProgram.getSymbolTable().getPrimarySymbol(table) == null) createLabel(table, label, true, SourceType.ANALYSIS);
            println((f != null ? "removed function " : "table ") + label + ": " + count + " pointer slots " + table + "-" + end + (complete ? " (already data)" : "") + " targets=" + targets.size());
        }
        println("RemoveTableFunctions: tables=" + tables.size() + " functions removed=" + removedFunctions + " tables (re)laid=" + laid);
    }
}
