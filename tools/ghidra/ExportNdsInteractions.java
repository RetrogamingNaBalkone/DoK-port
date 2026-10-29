import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.io.PrintWriter;

/** N11 read-only original NE exports; automatic C is evidence, never engine code. */
public class ExportNdsInteractions extends GhidraScript {
    public void run() throws Exception {
        boolean ru = getScriptArgs()[0].equals("ru");
        Path out = Path.of(getScriptArgs()[1]);
        Files.createDirectories(out);
        String expected = ru ? "12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28"
                             : "cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b";
        if (!expected.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalStateException("Ghidra program provenance SHA-256 differs");
        Files.writeString(out.resolve("program-source.tsv"), currentProgram.getName() + "\t" + expected + "\n", StandardCharsets.UTF_8);
        String[] segments = ru ? new String[]{"1068", "1098", "10f8", "10b0", "1190", "1038", "1180", "1008"}
                               : new String[]{"1020", "1030", "1060", "1040", "10d0", "1010", "10c0", "1008"};
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
        String[] ranges = ru ? new String[]{"1068:0000", "1068:4451", "1098:0309", "1098:0447", "10f8:0592", "10f8:09e0", "1098:4600", "1098:4f80", "10b0:0000", "10b0:05b2", "1008:0000", "1008:1010"}
                             : new String[]{"1020:0000", "1020:50b8", "1030:03ed", "1030:052b", "1060:06a2", "1060:0af0", "1030:4f00", "1030:5b10", "1040:0000", "1040:05be", "1008:0000", "1008:14ed"};
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
        println("NDS_INTERACTIONS_EXPORT_COMPLETE");
    }
}
