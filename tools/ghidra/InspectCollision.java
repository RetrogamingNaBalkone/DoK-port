import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/* G08 targeted original-code evidence; automatic C is not engine source. */
public class InspectCollision extends GhidraScript {
    public void run() throws Exception {
        boolean ru=getScriptArgs()[0].equals("ru");
        File dir=new File(getScriptArgs()[1]);dir.mkdirs();
        String[] ranges=ru?new String[]{"1068:0eb1","1068:10a5","1098:004f","1098:0ef6"}
            :new String[]{"1020:13bb","1020:15af","1030:0079","1030:0fda"};
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
        /* Unrelocated original instruction bytes. Collision instructions run
         * without intercepted callees; wrapper far calls bridge two CPUs. */
        try(FileOutputStream out=new FileOutputStream(new File(dir,"actor-segment.bin"))) {
            byte[] b=new byte[0x3000];currentProgram.getMemory().getBytes(toAddr(ru?"1068:0000":"1020:0000"),b);out.write(b);
        }
        try(FileOutputStream out=new FileOutputStream(new File(dir,"world-segment.bin"))) {
            byte[] b=new byte[0x1600];currentProgram.getMemory().getBytes(toAddr(ru?"1098:0000":"1030:0000"),b);out.write(b);
        }
        println("KREMLIN_COLLISION_COMPLETE");
    }
}

