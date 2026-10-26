import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.*;
import ghidra.program.model.pcode.JumpTable;
import ghidra.program.model.symbol.*;
import java.util.ArrayList;

/** Explicit CS switch destinations, checked against OG.EXE tables and CMP bounds.
 * Run BEFORE auto-analysis on a fresh MZ import. Does not patch executable bytes.
 */
public class PrepareOldGold extends GhidraScript {
    public void run() throws Exception {
        // entry, indirect jump, table, number of WORD entries; all Ghidra MZ addresses.
        String[][] switches = {
            {"1a0c:005c", "1a0c:0219", "1a0c:0724", "6"},
            {"1ae1:0003", "1ae1:0017", "1ae1:0142", "14"},
            {"1ae1:2eea", "1ae1:2fcf", "1ae1:30c5", "8"},
            {"20c4:03b0", "20c4:055d", "20c4:0d62", "4"}
        };
        for (String[] row : switches) {
            var entry = toAddr(row[0]);
            var jump = toAddr(row[1]);
            var table = toAddr(row[2]);
            int segment = ((SegmentedAddress)table).getSegment();
            int count = Integer.parseInt(row[3]);
            var space = (SegmentedAddressSpace)table.getAddressSpace();
            var destinations = new ArrayList<Address>();
            disassemble(entry);
            for (int i = 0; i < count; i++) {
                int offset = getShort(table.add(i*2)) & 0xffff;
                var target = space.getAddress(segment, offset);
                if (!currentProgram.getMemory().contains(target)) throw new Exception("Unmapped switch case");
                if (!destinations.contains(target)) destinations.add(target);
                currentProgram.getReferenceManager().addMemoryReference(jump, target,
                    RefType.COMPUTED_JUMP, SourceType.USER_DEFINED, 0);
                disassemble(target);
                println("OLD_GOLD_SWITCH " + jump + " case=" + i + " target=" + target);
            }
            var f = getFunctionAt(entry);
            if (f == null) f = createFunction(entry, null);
            if (f == null) throw new Exception("Cannot create switch owner " + entry);
            CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
            new JumpTable(jump, destinations, true, 0).writeOverride(f);
            setEOLComment(jump, "Verified original CS table " + table + ", " + count +
                " WORDs. Explicit targets bypass real-mode p-code CS normalization.");
        }
        println("OLD_GOLD_SWITCHES_PREPARED");
    }
}
