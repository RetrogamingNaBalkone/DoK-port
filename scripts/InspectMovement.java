import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/* G07 targeted original-code evidence; automatic C is not engine source. */
public class InspectMovement extends GhidraScript {
    public void run() throws Exception {
        boolean ru=getScriptArgs()[0].equals("ru");
        File dir=new File(getScriptArgs()[1]);dir.mkdirs();
        String[] ranges=ru?new String[]{"1068:0000","1068:16fb","1068:2127","1068:2350","1070:0000","1070:0500","10c0:1639","10c0:1c00","1098:0e13","1098:0ef6"}
            :new String[]{"1020:04c0","1020:1c20","1020:26d7","1020:2900","1028:0000","1028:0500","1068:199c","1068:2000","1030:0ef7","1030:0fda"};
        DecompInterface dec=new DecompInterface();dec.openProgram(currentProgram);
        try(PrintWriter asm=new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8));
            PrintWriter c=new PrintWriter(Files.newBufferedWriter(new File(dir,"automatic.c").toPath(),StandardCharsets.UTF_8))) {
            for(int k=0;k<ranges.length;k+=2) {
                var start=toAddr(ranges[k]);var end=toAddr(ranges[k+1]);
                for(Instruction ins:currentProgram.getListing().getInstructions(start,true)) {
                    if(ins.getAddress().compareTo(end)>0) break;
                    asm.println(ins.getAddress()+"\t"+ins);
                }
                for(Function f:currentProgram.getFunctionManager().getFunctions(start,true)) {
                    if(f.getEntryPoint().compareTo(end)>0) break;
                    c.println("/* "+f.getEntryPoint()+" automatic, signature unverified */");
                    var result=dec.decompileFunction(f,60,monitor);
                    if(result.decompileCompleted()) c.println(result.getDecompiledFunction().getC());
                }
            }
        } finally {dec.dispose();}
        /* Unrelocated original instruction bytes, kept beside the listing for
         * controlled CPU execution with clock/collision callees intercepted. */
        try(FileOutputStream out=new FileOutputStream(new File(dir,"actor-segment.bin"))) {
            byte[] b=new byte[0x3000];currentProgram.getMemory().getBytes(toAddr(ru?"1068:0000":"1020:0000"),b);out.write(b);
        }
        try(FileOutputStream out=new FileOutputStream(new File(dir,"world-segment.bin"))) {
            byte[] b=new byte[0x1600];currentProgram.getMemory().getBytes(toAddr(ru?"1098:0000":"1030:0000"),b);out.write(b);
        }
        try(PrintWriter out=new PrintWriter(new File(dir,"event-descriptor.tsv"))) {
            var a=toAddr(ru?"1190:0512":"10d0:0522");
            out.printf("%s\t%04x:%04x\t%08x%n",a,getShort(a.add(2))&65535,getShort(a)&65535,getInt(a.add(4)));
        }
        println("KREMLIN_MOVEMENT_COMPLETE");
    }
}
