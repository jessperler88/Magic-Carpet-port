// Clear stale CALL_RETURN flow overrides on JMP instructions.
//
// When the analyser (or MergeJumpFragments/DeleteStubs) removes a bogus function
// that a jump used to "tail-call", the jump keeps its CALL_RETURN override and
// now points at a plain address inside its own function.  The decompiler
// treats that as a call to a non-function and the decompiler process dies
// (creature_check_terrain_102b0 was the first victim).  This script resets the
// override on every JMP whose target is not a function entry and lies inside
// the same function (or in no function at all), disassembles the target if
// needed, and recomputes the affected bodies.  Jumps into the middle of a
// *different* function (shared tails such as crt_strtoul -> crt_strtol) and
// real tail calls to function entries keep their override and are reported.
//   -postScript ClearJumpOverrides.java
//@category MagicCarpet
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class ClearJumpOverrides extends GhidraScript {
    @Override
    protected void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        Set<Function> touched = new LinkedHashSet<>();
        int cleared = 0, keptTail = 0, keptShared = 0, other = 0;
        for (Instruction ins : listing.getInstructions(true)) {
            if (ins.getFlowOverride() == FlowOverride.NONE) continue;
            if (!ins.getMnemonicString().equalsIgnoreCase("JMP")) { other++; println("kept non-JMP override " + ins.getAddress() + " " + ins.getFlowOverride() + " " + ins); continue; }
            Function owner = fm.getFunctionContaining(ins.getAddress());
            Address target = null;
            for (Reference r : ins.getReferencesFrom()) {
                if (r.getReferenceType().isFlow() && !r.getReferenceType().isFallthrough()) { target = r.getToAddress(); break; }
            }
            if (target == null) { other++; println("kept override without flow target " + ins.getAddress() + " " + ins); continue; }
            if (fm.getFunctionAt(target) != null) { keptTail++; continue; }               // genuine tail call
            Function targetOwner = fm.getFunctionContaining(target);
            if (targetOwner != null && targetOwner != owner) {                              // jump into another function's middle
                keptShared++;
                println("kept shared-tail override " + ins.getAddress() + " in " + (owner == null ? "-" : owner.getName()) + " -> " + target + " inside " + targetOwner.getName());
                continue;
            }
            ins.setFlowOverride(FlowOverride.NONE);
            if (ins.isFallThroughOverridden()) ins.clearFallThroughOverride();
            cleared++;
            println("cleared " + ins.getAddress() + " " + ins + " in " + (owner == null ? "-" : owner.getName()));
            if (listing.getInstructionAt(target) == null && listing.getDefinedDataAt(target) == null) {
                new DisassembleCommand(target, null, true).applyTo(currentProgram, monitor);
            }
            if (owner != null) touched.add(owner);
        }
        println("ClearJumpOverrides: cleared=" + cleared + " kept tail-calls=" + keptTail + " kept shared-tails=" + keptShared + " other=" + other);
        for (int pass = 0; pass < 3; pass++) {
            int changed = 0;
            for (Function f : new ArrayList<>(touched)) {
                long before = f.getBody().getNumAddresses();
                try { CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor); } catch (Exception ex) { println("fixup failed " + f.getName() + ": " + ex.getMessage()); continue; }
                Function g = fm.getFunctionAt(f.getEntryPoint());
                if (g != null && g.getBody().getNumAddresses() != before) { changed++; println("  body " + g.getName() + " " + before + " -> " + g.getBody().getNumAddresses()); }
            }
            if (changed == 0) break;
        }
    }
}
