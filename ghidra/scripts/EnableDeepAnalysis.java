// Pre-script: switch on the analyzers that matter for a stripped Watcom
// DOS/4GW binary but are off by default.
//@category MagicCarpet
import ghidra.app.script.GhidraScript;

public class EnableDeepAnalysis extends GhidraScript {
    @Override
    public void run() throws Exception {
        setAnalysisOption(currentProgram, "Aggressive Instruction Finder", "false"); // spun for 30+ min on this binary; use FindGapFunctions instead
        setAnalysisOption(currentProgram, "Decompiler Parameter ID", "true");
        setAnalysisOption(currentProgram, "Decompiler Switch Analysis", "true");
        setAnalysisOption(currentProgram, "Non-Returning Functions - Discovered", "true");
        setAnalysisOption(currentProgram, "Shared Return Calls", "true");
        println("deep analysis options enabled for " + currentProgram.getName());
    }
}
