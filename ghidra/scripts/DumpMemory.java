// Write every initialized memory block to <outDir>/<block>_<start>.bin for
// byte-level comparison with external tools.   -postScript DumpMemory.java <outDir>
//@category MagicCarpet
import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.MemoryBlock;
import java.io.File;
import java.io.FileOutputStream;

public class DumpMemory extends GhidraScript {
    @Override
    protected void run() throws Exception {
        File dir = new File(getScriptArgs()[0]);
        dir.mkdirs();
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (!b.isInitialized()) continue;
            byte[] buf = new byte[(int) b.getSize()];
            b.getBytes(b.getStart(), buf);
            File f = new File(dir, b.getName().replace('.', '_') + "_" + b.getStart() + ".bin");
            try (FileOutputStream o = new FileOutputStream(f)) { o.write(buf); }
            println("wrote " + f + " (" + buf.length + " bytes)");
        }
    }
}
