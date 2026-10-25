import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/* Read-only Ghidra NE-loader addresses, original PKR.EXE RUS. */
public class InspectCadence extends GhidraScript {
    public void run() throws Exception {
        File dir = new File(getScriptArgs()[0]); dir.mkdirs();
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8))) {
            String[] ranges = {"1008:0d6a","1008:0ffc","1068:1657","1068:16f6","1110:0329","1110:03c2","1120:08dd","1120:0a31","1138:051d","1138:0590","1138:086a","1138:0a5a"};
            for(int k=0;k<ranges.length;k+=2) {
                for(Instruction ins:currentProgram.getListing().getInstructions(toAddr(ranges[k]),true)) {
                    if(ins.getAddress().compareTo(toAddr(ranges[k+1]))>0) break;
                    out.println(ins.getAddress()+"\t"+ins);
                }
            }
            for(int off=0xe10;off<0xe50;off+=4) {
                var a=toAddr(String.format("1190:%04x",off));
                out.printf("vtable %s %04x:%04x%n",a,getShort(a.add(2))&65535,getShort(a)&65535);
            }
            var cb=toAddr("1190:0512");
            out.printf("player event descriptor %08x %08x%n",getInt(cb),getInt(cb.add(4)));
            var fn=toAddr(String.format("%04x:%04x",getShort(cb.add(2))&65535,getShort(cb)&65535));
            Function f=getFunctionAt(fn);
            if(f!=null) for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true)) out.println(ins.getAddress()+"\t"+ins);
        }
        println("KREMLIN_CADENCE_COMPLETE");
    }
}
