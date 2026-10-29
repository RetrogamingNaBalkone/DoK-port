import ghidra.app.script.GhidraScript;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;

/** N08: read-only export of relocated NE memory for an isolated x86 oracle. */
public class ExportNdsTime extends GhidraScript {
    public void run() throws Exception {
        boolean ru = getScriptArgs()[0].equals("ru");
        Path out = Path.of(getScriptArgs()[1]);
        Files.createDirectories(out);
        String expected = ru ? "12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28"
                             : "cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b";
        if (!expected.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalStateException("Ghidra program provenance SHA-256 differs");
        Files.writeString(out.resolve("program-source.tsv"),
                currentProgram.getName() + "\t" + currentProgram.getExecutableSHA256() + "\n",
                StandardCharsets.UTF_8);
        String[] segments = ru ? new String[]{"1000", "1038", "1130"}
                               : new String[]{"1000", "1010", "1078"};
        StringBuilder manifest = new StringBuilder("segment\tlength\tghidra_start\n");
        for (String segment : segments) {
            var start = toAddr(segment + ":0000");
            var block = currentProgram.getMemory().getBlock(start);
            if (block == null || !block.getStart().equals(start))
                throw new IllegalStateException("Missing exact segment " + segment);
            byte[] bytes = new byte[(int)block.getSize()];
            currentProgram.getMemory().getBytes(start, bytes);
            Files.write(out.resolve(segment + ".bin"), bytes);
            manifest.append(segment).append('\t').append(bytes.length).append('\t')
                    .append(start).append('\n');
        }
        Files.writeString(out.resolve("segments.tsv"), manifest, StandardCharsets.UTF_8);
        println("NDS_TIME_EXPORT_COMPLETE");
    }
}
