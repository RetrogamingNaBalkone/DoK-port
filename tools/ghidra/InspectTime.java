import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/* G06: reproducible evidence, automatic pseudocode is not engine source. */
public class InspectTime extends GhidraScript {
    public void run() throws Exception {
        boolean ru=getScriptArgs()[0].equals("ru");
        File dir=new File(getScriptArgs()[1]);dir.mkdirs();
        String[] ranges=ru ? new String[]{"1030:0000","1030:007f","1038:0000","1038:0445","1130:0000","1130:0377"}
            : new String[]{"1010:132a","1010:13a9","1010:13aa","1010:17db","1078:168a","1078:1a0d"};
        DecompInterface dec=new DecompInterface();dec.openProgram(currentProgram);
        disassemble(toAddr(ru?"1130:0000":"1078:168a"));
        disassemble(toAddr(ru?"1038:0234":"1010:15ea"));
        try(PrintWriter asm=new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8));
            PrintWriter c=new PrintWriter(Files.newBufferedWriter(new File(dir,"automatic.c").toPath(),StandardCharsets.UTF_8));
            PrintWriter refs=new PrintWriter(Files.newBufferedWriter(new File(dir,"callers.tsv").toPath(),StandardCharsets.UTF_8))) {
            for(int k=0;k<ranges.length;k+=2) {
                var start=toAddr(ranges[k]);var end=toAddr(ranges[k+1]);
                /* Raw range includes interrupt handlers and missed function tails. */
                for(Instruction ins:currentProgram.getListing().getInstructions(start,true)) {
                    if(ins.getAddress().compareTo(end)>0) break;
                    asm.println(ins.getAddress()+"\t"+ins);
                }
                for(Function f:currentProgram.getFunctionManager().getFunctions(start,true)) {
                    if(f.getEntryPoint().compareTo(end)>0) break;
                    c.println("/* "+f.getEntryPoint()+" automatic, signature unverified */");
                    var result=dec.decompileFunction(f,60,monitor);
                    if(result.decompileCompleted()) c.println(result.getDecompiledFunction().getC());
                    for(Function caller:f.getCallingFunctions(monitor))
                        refs.println(f.getEntryPoint()+"\t"+caller.getEntryPoint()+"\t"+caller.getName());
                }
            }
            for(String entry: new String[]{"1000:5cb3",ru?"1068:0421":"1020:0901",ru?"1068:04ae":"1020:0998"}) {
                Function f=getFunctionAt(toAddr(entry));
                if(f==null) continue;
                for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true))
                    asm.println(ins.getAddress()+"\t"+ins);
            }
        } finally {dec.dispose();}
        try(PrintWriter data=new PrintWriter(new File(dir,"clock-data.tsv"))) {
            for(int off: new int[]{0xe70,0xe74,0xe78,0xe7c,0x3e8,0x3ea}) {
                var a=toAddr((ru?"1190:":"10d0:")+Integer.toHexString(off+(ru?0:(off<0xe00?8:0xb0))));
                data.println(a+"\t"+Integer.toUnsignedString(currentProgram.getMemory().getInt(a)));
            }
        }
        println("KREMLIN_TIME_COMPLETE");
    }
}
