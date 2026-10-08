// Pre-script: attach a user FunctionID database (.fidb) so the "Function ID"
// analyzer can match library functions during analysis.
//   -preScript AttachFid.java <path-to.fidb>
//@category MagicCarpet
import ghidra.app.script.GhidraScript;
import ghidra.feature.fid.db.FidFile;
import ghidra.feature.fid.db.FidFileManager;
import java.io.File;

public class AttachFid extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        if (a.length < 1) { printerr("usage: AttachFid <file.fidb>"); return; }
        File f = new File(a[0]);
        FidFileManager mgr = FidFileManager.getInstance();
        boolean present = false;
        for (FidFile ff : mgr.getUserAddedFiles()) if (ff.getPath().equals(f.getAbsolutePath())) present = true;
        if (!present) mgr.addUserFidFile(f);
        for (FidFile ff : mgr.getFidFiles()) if (ff.getPath().equals(f.getAbsolutePath())) ff.setActive(true);
        setAnalysisOption(currentProgram, "Function ID", "true");
        println("FID attached: " + f + " (" + mgr.getUserAddedFiles().size() + " user fid files)");
    }
}
