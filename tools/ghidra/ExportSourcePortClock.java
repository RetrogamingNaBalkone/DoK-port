// Export existing analyzed bytes/instructions only; never reanalyse or patch.
//@category DoK.SourcePort
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;

public class ExportSourcePortClock extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) throw new IllegalArgumentException("output start end-exclusive");
        Path output = Path.of(args[0]);
        Files.createDirectories(output);
        Address start = toAddr(args[1]), end = toAddr(args[2]);
        byte[] bytes = new byte[(int)end.subtract(start)];
        if (currentProgram.getMemory().getBytes(start, bytes) != bytes.length)
            throw new IllegalStateException("Short memory read");
        Files.write(output.resolve("clock.bin"), bytes);
        StringBuilder listing = new StringBuilder();
        Address address = start;
        while (address.compareTo(end) < 0) {
            Instruction instruction = currentProgram.getListing().getInstructionAt(address);
            if (instruction == null) throw new IllegalStateException("No instruction at " + address);
            listing.append(address).append('\t').append(instruction).append('\n');
            address = address.add(instruction.getLength());
        }
        if (!address.equals(end)) throw new IllegalStateException("End is inside an instruction");
        Files.writeString(output.resolve("clock.asm"), listing, StandardCharsets.UTF_8);
        println("SOURCE_PORT_CLOCK_EXPORTED");
    }
}
