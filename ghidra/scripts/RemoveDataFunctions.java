// Delete "functions" whose entry lies in the data object (>= 0x90000 in
// carpet.exe): the auto-analyser occasionally disassembles pointer tables there.
//   -postScript RemoveDataFunctions.java [dataStart]   default 0x90000
//@category MagicCarpet
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.util.ArrayList;
import java.util.List;

public class RemoveDataFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        long dataStart = getScriptArgs().length > 0 ? Long.decode(getScriptArgs()[0]) : 0x90000L;
        List<Address> victims = new ArrayList<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.getEntryPoint().getOffset() >= dataStart) victims.add(f.getEntryPoint());
        }
        for (Address a : victims) {
            Function f = currentProgram.getFunctionManager().getFunctionAt(a);
            println("removing " + f.getName() + " and its code units");
            currentProgram.getFunctionManager().removeFunction(a);
            currentProgram.getListing().clearCodeUnits(f.getBody().getMinAddress(), f.getBody().getMaxAddress(), false);
        }
        println("RemoveDataFunctions: removed " + victims.size());
    }
}
