// Build a FunctionID database from a project folder of imported library
// object modules (e.g. Open Watcom clib3r.lib members) and attach it so later
// analysis of carpet.exe auto-names C runtime functions.
//
// Usage (headless, run as a postScript on any program in the project):
//   -postScript BuildWatcomFid.java <fidb path> <libname> <version> <variant> <project folder> <languageID>
// e.g.  C:\Magic Carpet\ghidra\fid\watcom.fidb clib3r 2.0 dos /watcom x86:LE:32:default
//@category MagicCarpet
import ghidra.app.script.GhidraScript;
import ghidra.feature.fid.db.FidDB;
import ghidra.feature.fid.db.FidFile;
import ghidra.feature.fid.db.FidFileManager;
import ghidra.feature.fid.service.FidPopulateResult;
import ghidra.feature.fid.service.FidService;
import ghidra.framework.model.DomainFile;
import ghidra.framework.model.DomainFolder;
import ghidra.program.database.ProgramContentHandler;
import ghidra.program.model.lang.LanguageID;
import ghidra.util.task.TaskMonitor;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

public class BuildWatcomFid extends GhidraScript {

    private void findPrograms(ArrayList<DomainFile> programs, DomainFolder folder) throws Exception {
        for (DomainFile f : folder.getFiles()) {
            if (f.getContentType().equals(ProgramContentHandler.PROGRAM_CONTENT_TYPE)) programs.add(f);
        }
        for (DomainFolder sub : folder.getFolders()) findPrograms(programs, sub);
    }

    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        if (a.length < 6) {
            printerr("usage: BuildWatcomFid <fidb> <libname> <version> <variant> <projectFolder> <languageID>");
            return;
        }
        File dbFile = new File(a[0]);
        File done = new File(a[0] + ".done");
        if (done.exists()) { return; }  // headless runs us once per program; populate only once
        FidFileManager mgr = FidFileManager.getInstance();
        if (!dbFile.exists()) {
            dbFile.getParentFile().mkdirs();
            mgr.createNewFidDatabase(dbFile);
        }
        mgr.addUserFidFile(dbFile);
        FidFile fidFile = null;
        for (FidFile f : mgr.getUserAddedFiles()) {
            if (f.getPath().equals(dbFile.getAbsolutePath())) fidFile = f;
        }
        if (fidFile == null) {
            List<FidFile> files = mgr.getUserAddedFiles();
            if (files.isEmpty()) throw new Exception("could not attach fidb");
            fidFile = files.get(files.size() - 1);
        }
        DomainFolder root = state.getProject().getProjectData().getFolder(a[4]);
        if (root == null) throw new Exception("project folder not found: " + a[4]);
        ArrayList<DomainFile> programs = new ArrayList<>();
        findPrograms(programs, root);
        println("populating " + a[1] + ":" + a[2] + ":" + a[3] + " from " + programs.size() + " programs");
        FidService service = new FidService();
        FidDB db = fidFile.getFidDB(true);
        try {
            FidPopulateResult result = service.createNewLibraryFromPrograms(db, a[1], a[2], a[3], programs,
                    null, new LanguageID(a[5]), null, null, TaskMonitor.DUMMY);
            println("FID: added " + result.getTotalAttempted() + " attempted, " + result.getTotalAdded() + " added, "
                    + result.getTotalExcluded() + " excluded");
            db.saveDatabase("Saving", monitor);
            new java.io.FileWriter(done).close();
        } finally {
            db.close();
        }
    }
}
