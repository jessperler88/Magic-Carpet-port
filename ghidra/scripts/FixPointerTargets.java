// Make every code address that is referenced by a relocated dword in the data
// object (Thing class/model handler tables at 0x943da.., etc.) a function start.
// Cases:
//   * target undefined                     -> disassemble + create function
//   * target inside a function whose entry is <=16 bytes before it and which
//     has no callers (the gap finder started it on padding bytes) -> remove
//     that function, clear the bytes before the target, recreate at target
//   * target inside a function further in  -> create a function at target
//     (Ghidra re-bounds the enclosing one)
//   -postScript FixPointerTargets.java <targets.txt>   (lines: target,source hex)
//@category MagicCarpet
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class FixPointerTargets extends GhidraScript {
    @Override
    protected void run() throws Exception {
        List<String> lines = Files.readAllLines(Paths.get(getScriptArgs()[0]));
        Listing listing = currentProgram.getListing();
        int created = 0, rebased = 0, already = 0, failed = 0;
        for (String line : lines) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] p = line.split(",");
            Address t = toAddr(Long.parseLong(p[0], 16));
            Address src = p.length > 1 ? toAddr(Long.parseLong(p[1], 16)) : null;
            if (src != null) {
                currentProgram.getReferenceManager().addMemoryReference(src, t, RefType.DATA, SourceType.ANALYSIS, 0);
            }
            Function f = listing.getFunctionContaining(t);
            if (f != null && f.getEntryPoint().equals(t)) { already++; continue; }
            if (f != null) {
                long off = t.subtract(f.getEntryPoint());
                boolean noCallers = f.getSymbol().getReferenceCount() == 0;
                if (off <= 16 && noCallers) {
                    Address fs = f.getEntryPoint();
                    currentProgram.getFunctionManager().removeFunction(fs);
                    listing.clearCodeUnits(fs, t.subtract(1), false);
                    rebased++;
                }
            }
            if (listing.getInstructionAt(t) == null) {
                if (listing.getDefinedDataContaining(t) != null) listing.clearCodeUnits(t, t, false);
                DisassembleCommand dc = new DisassembleCommand(t, null, true);
                dc.applyTo(currentProgram, monitor);
            }
            CreateFunctionCmd fc = new CreateFunctionCmd(t);
            if (fc.applyTo(currentProgram, monitor)) created++;
            else { failed++; println("could not create function at " + t); }
        }
        println("FixPointerTargets: already=" + already + " created=" + created + " (rebased " + rebased + ") failed=" + failed);
        if (created > 0) analyzeAll(currentProgram);
    }
}
