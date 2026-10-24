import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Read-only evidence export; does not change signatures or labels. */
public class InspectEnemyFields extends GhidraScript {
    public void run() throws Exception {
        File dir = new File(getScriptArgs()[0]); dir.mkdirs();
        DecompInterface dec = new DecompInterface(); dec.openProgram(currentProgram);
        try (PrintWriter c = new PrintWriter(Files.newBufferedWriter(new File(dir,"functions.c").toPath(),StandardCharsets.UTF_8));
             PrintWriter asm = new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8))) {
            for (int n=1;n<getScriptArgs().length;n++) {
                String addr=getScriptArgs()[n]; Function f=getFunctionAt(toAddr(addr));
                if(f==null) throw new IOException("Missing function "+addr);
                var result=dec.decompileFunction(f,90,monitor);
                if(!result.decompileCompleted()) throw new IOException(result.getErrorMessage());
                c.println("/* "+addr+" */"); c.println(result.getDecompiledFunction().getC());
                asm.println("FUNCTION "+addr+" "+f.getName());
                for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true)) asm.println(ins.getAddress()+"\t"+ins);
            }
        } finally {dec.dispose();}
        println("ENEMY_FIELDS_COMPLETE "+currentProgram.getName());
    }
}
