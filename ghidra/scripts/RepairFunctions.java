// (Re)create functions at the listed addresses without running auto-analysis
// afterwards.  Used to restore functions that an analysis pass removed (one
// byte `ret` stubs shared by several callers, small helpers absorbed into a
// neighbour) and to force-create handler entries that CreateFunctionCmd
// refused while the bytes were owned by another function's body.
//   -postScript RepairFunctions.java <addresses.txt>   (one hex address per line, '#' comments)
//@category MagicCarpet
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class RepairFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        List<String> lines = Files.readAllLines(Paths.get(getScriptArgs()[0]));
        Listing listing = currentProgram.getListing();
        int ok = 0, already = 0, failed = 0;
        for (String line : lines) {
            line = line.split("#")[0].trim();
            if (line.isEmpty()) continue;
            Address t = toAddr(Long.parseLong(line.replace("0x", ""), 16));
            Function f = listing.getFunctionAt(t);
            if (f != null) { already++; continue; }
            Function enclosing = listing.getFunctionContaining(t);
            if (enclosing != null) {
                // carve the target out of the enclosing body so the new function owns it
                AddressSet body = new AddressSet(enclosing.getBody());
                AddressSet tail = new AddressSet(t, enclosing.getBody().getMaxAddress());
                body.delete(tail);
                try { enclosing.setBody(body); } catch (Exception ex) { println("setBody failed for " + enclosing.getName() + ": " + ex.getMessage()); }
            }
            Instruction ins = listing.getInstructionAt(t);
            if (ins == null) {
                listing.clearCodeUnits(t, t, false);
                new DisassembleCommand(t, null, true).applyTo(currentProgram, monitor);
                ins = listing.getInstructionAt(t);
            }
            if (ins == null) { failed++; println("no instruction at " + t); continue; }
            CreateFunctionCmd fc = new CreateFunctionCmd(null, t, null, ghidra.program.model.symbol.SourceType.USER_DEFINED);
            if (fc.applyTo(currentProgram, monitor)) ok++;
            else {
                // last resort: a one-address body
                try {
                    currentProgram.getFunctionManager().createFunction(null, t, new AddressSet(t, t), ghidra.program.model.symbol.SourceType.USER_DEFINED);
                    ok++;
                } catch (Exception ex) { failed++; println("could not create function at " + t + ": " + ex.getMessage()); }
            }
        }
        println("RepairFunctions: already=" + already + " created=" + ok + " failed=" + failed);
    }
}
