import ghidra.app.script.GhidraScript;
import java.util.HexFormat;

/** Diagnostic for far-call and switch interpretation; original bytes remain unchanged. */
public class InspectOldGoldContext extends GhidraScript {
    public void run() throws Exception {
        var ctx = currentProgram.getProgramContext();
        for (String value : new String[]{"1000:014c", "20c4:03b0", "20c4:055d", "20c4:0d62", "1ae1:0003"}) {
            var a = toAddr(value);
            byte[] data = new byte[12];
            currentProgram.getMemory().getBytes(a, data);
            println(value+" bytes="+HexFormat.of().formatHex(data)+" cs="+ctx.getValue(ctx.getRegister("CS"), a, false));
            var ins = getInstructionAt(a);
            if (ins != null) for (var op : ins.getPcode()) println(op.toString());
        }
    }
}
