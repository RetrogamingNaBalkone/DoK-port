import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.io.PrintWriter;

/** N13 read-only original NE exports; automatic C is evidence, never engine code. */
public class ExportNdsEffects extends GhidraScript {
    public void run() throws Exception {
        boolean ru = getScriptArgs()[0].equals("ru");
        Path out = Path.of(getScriptArgs()[1]);
        Files.createDirectories(out);
        String expected = ru ? "12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28"
                             : "cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b";
        if (!expected.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalStateException("Ghidra program provenance SHA-256 differs");
        Files.writeString(out.resolve("program-source.tsv"), currentProgram.getName() + "\t" + expected + "\n", StandardCharsets.UTF_8);
        String[] segments = ru ? new String[]{"1068", "1098", "10f8", "10b0", "1190", "1038", "1180", "1008", "1050", "1088", "1120", "10b8", "1110", "1000", "1010"}
                               : new String[]{"1020", "1030", "1060", "1040", "10d0", "1010", "10c0", "1008", "1018", "1028", "1078", "1048", "1068", "1000"};
        StringBuilder manifest = new StringBuilder("segment\tlength\tghidra_start\n");
        for (String segment : segments) {
            var start = toAddr(segment + ":0000");
            var block = currentProgram.getMemory().getBlock(start);
            if (block == null || !block.getStart().equals(start)) throw new IllegalStateException("Missing exact segment " + segment);
            byte[] bytes = new byte[(int)block.getSize()];
            currentProgram.getMemory().getBytes(start, bytes);
            Files.write(out.resolve(segment + ".bin"), bytes);
            manifest.append(segment).append('\t').append(bytes.length).append('\t').append(start).append('\n');
        }
        Files.writeString(out.resolve("segments.tsv"), manifest, StandardCharsets.UTF_8);
        String[] ranges = ru ? new String[]{"1050:0000", "1050:03ff", "1068:02f5", "1068:07ef", "1068:36b1", "1068:3746", "10b0:0ccb", "10b0:1d00", "1098:30c1", "1098:5460", "1088:0000", "1088:05fc", "10f8:0592", "10f8:0b00", "1120:0b12", "1120:15f0", "10b8:023e", "10b8:04ef", "1110:0329", "1110:0649", "1008:0719", "1008:13c3", "1010:0000", "1010:01c9"}
                             : new String[]{"1018:0f0e", "1018:1394", "1020:07cf", "1020:0ccf", "1020:3e14", "1020:3eaf", "1040:0ce3", "1040:1d40", "1030:3571", "1030:6160", "1028:1382", "1028:19fe", "1060:06a2", "1060:0c10", "1078:0b18", "1078:15f6", "1048:023e", "1048:04ef", "1068:0335", "1068:075f", "1008:0719", "1008:1a42", "1010:0000", "1010:01c9"};
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        try (PrintWriter asm = new PrintWriter(Files.newBufferedWriter(out.resolve("evidence.asm"), StandardCharsets.UTF_8));
             PrintWriter automatic = new PrintWriter(Files.newBufferedWriter(out.resolve("automatic.c"), StandardCharsets.UTF_8))) {
            for (int k = 0; k < ranges.length; k += 2) {
                var start = toAddr(ranges[k]); var end = toAddr(ranges[k + 1]);
                for (Instruction ins : currentProgram.getListing().getInstructions(start, true)) {
                    if (ins.getAddress().compareTo(end) > 0) break;
                    asm.println(ins.getAddress() + "\t" + ins);
                }
                for (Function f : currentProgram.getFunctionManager().getFunctions(start, true)) {
                    if (f.getEntryPoint().compareTo(end) > 0) break;
                    automatic.println("/* " + f.getEntryPoint() + " automatic, signature unverified */");
                    var result = dec.decompileFunction(f, 60, monitor);
                    if (result.decompileCompleted()) automatic.println(result.getDecompiledFunction().getC());
                }
            }
        } finally { dec.dispose(); }
        println("NDS_EFFECTS_EXPORT_COMPLETE");
    }
}
