// Create struct data types and type global variables from a text definition
// file, so the decompiler shows field names instead of raw offsets.
//
//   -postScript ApplyTypes.java <types.txt>
//
// File format ('#' starts a comment, blank lines ignored, order matters for
// struct references):
//   struct <Name> <size>                 start a struct of that size (hex ok)
//     <offset> <field> <type> [comment]  field inside the current struct
//   global <addr> <name> <type> [comment] data + primary label at an address
//   label <addr> <name> [comment]        plain label
// Types: u8 u16 u32 i8 i16 i32 char void, a struct name, with any number of
// trailing '*' (pointer) and '[n]' (array) suffixes, e.g. Thing[1000], char*[58].
//@category MagicCarpet
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class ApplyTypes extends GhidraScript {
    private DataTypeManager dtm;
    private final Map<String, DataType> types = new HashMap<>();
    private final CategoryPath cat = new CategoryPath("/carpet");

    private DataType base(String n) {
        switch (n) {
            case "u8": return UnsignedCharDataType.dataType;
            case "i8": return SignedCharDataType.dataType;
            case "char": return CharDataType.dataType;
            case "u16": return UnsignedShortDataType.dataType;
            case "i16": return ShortDataType.dataType;
            case "u32": return UnsignedIntegerDataType.dataType;
            case "i32": return IntegerDataType.dataType;
            case "void": return VoidDataType.dataType;
            default:
                DataType t = types.get(n);
                if (t == null) throw new IllegalArgumentException("unknown type " + n);
                return t;
        }
    }

    private DataType parse(String spec) {
        // peel suffixes from the right: [n] then * (so "char*[58]" = array of 58 pointers)
        spec = spec.trim();
        if (spec.endsWith("]")) {
            int i = spec.lastIndexOf('[');
            int n = Integer.decode(spec.substring(i + 1, spec.length() - 1));
            DataType el = parse(spec.substring(0, i));
            return new ArrayDataType(el, n, el.getLength(), dtm);
        }
        if (spec.endsWith("*")) {
            DataType to = parse(spec.substring(0, spec.length() - 1));
            return dtm.getPointer(to, 4);
        }
        return base(spec);
    }

    @Override
    protected void run() throws Exception {
        dtm = currentProgram.getDataTypeManager();
        Listing listing = currentProgram.getListing();
        List<String> lines = Files.readAllLines(Paths.get(getScriptArgs()[0]));
        StructureDataType cur = null;
        int structs = 0, fields = 0, globals = 0, labels = 0;
        for (String raw : lines) {
            String line = raw.split("#", 2)[0].trim();
            String comment = raw.contains("#") ? raw.substring(raw.indexOf('#') + 1).trim() : "";
            if (line.isEmpty()) continue;
            String[] p = line.split("\\s+");
            if (p[0].equals("struct")) {
                if (cur != null) { types.put(cur.getName(), dtm.addDataType(cur, DataTypeConflictHandler.REPLACE_HANDLER)); }
                cur = new StructureDataType(cat, p[1], Integer.decode(p[2]), dtm);
                types.put(p[1], cur); // allow self references through pointers
                structs++;
            } else if (p[0].equals("global") || p[0].equals("label")) {
                if (cur != null) { types.put(cur.getName(), dtm.addDataType(cur, DataTypeConflictHandler.REPLACE_HANDLER)); cur = null; }
                Address addr = toAddr(Long.decode(p[1]));
                if (p[0].equals("global")) {
                    DataType dt = parse(p[3]);
                    int len = dt.getLength();
                    listing.clearCodeUnits(addr, addr.add(Math.max(len, 1) - 1), false);
                    try { listing.createData(addr, dt); globals++; }
                    catch (Exception ex) { printerr("createData failed at " + addr + " " + p[2] + ": " + ex.getMessage()); }
                } else labels++;
                createLabel(addr, p[2], true, SourceType.USER_DEFINED);
                if (!comment.isEmpty()) listing.setComment(addr, CodeUnit.EOL_COMMENT, comment);
            } else if (cur != null) {
                int off = Integer.decode(p[0]);
                DataType dt = parse(p[2]);
                try { cur.replaceAtOffset(off, dt, dt.getLength(), p[1], comment); fields++; }
                catch (Exception ex) { printerr("field " + cur.getName() + "." + p[1] + " at " + p[0] + ": " + ex.getMessage()); }
            } else printerr("line outside struct: " + raw);
        }
        if (cur != null) types.put(cur.getName(), dtm.addDataType(cur, DataTypeConflictHandler.REPLACE_HANDLER));
        println("ApplyTypes: structs=" + structs + " fields=" + fields + " globals=" + globals + " labels=" + labels);
    }
}
