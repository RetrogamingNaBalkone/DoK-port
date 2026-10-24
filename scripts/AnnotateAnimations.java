import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class AnnotateAnimations extends GhidraScript {
    public void run() throws Exception {
        String lang=getScriptArgs()[0]; boolean ru=lang.equals("ru");
        if(!ru && !lang.equals("en")) throw new IOException("Expected ru or en");
        CategoryPath path=new CategoryPath("/DoK/Native16");
        DataType far=currentProgram.getDataTypeManager().getDataType(path,"Far16Token");
        if(far==null || far.getLength()!=4) throw new IOException("Run stage 2 annotations first");
        StructureDataType header=new StructureDataType(path,"AnimationHeader8",8);
        header.setPackingEnabled(false);
        header.replaceAtOffset(0,ShortDataType.dataType,2,"frame_count","Signed loop bound; positive in all original resources");
        header.replaceAtOffset(2,WordDataType.dataType,2,"unknown_word","Zero in observed files; semantic meaning unconfirmed");
        header.replaceAtOffset(4,far,4,"runtime_frames","Loader replaces this with base+8");
        StructureDataType frame=new StructureDataType(path,"AnimationFrame10",10);
        frame.setPackingEnabled(false);
        frame.replaceAtOffset(0,ShortDataType.dataType,2,"interval_ticks","Transferred to object+52h; sign-extended by scheduling callers");
        frame.replaceAtOffset(2,WordDataType.dataType,2,"parameter_a","Transferred to object+20h, or projectile+54h; meaning unconfirmed");
        frame.replaceAtOffset(4,WordDataType.dataType,2,"parameter_b","Transferred to object+22h, or projectile+56h; meaning unconfirmed");
        frame.replaceAtOffset(6,far,4,"runtime_image","Rebuilt from sequential image position; never a file offset");
        currentProgram.getDataTypeManager().resolve(header,DataTypeConflictHandler.REPLACE_HANDLER);
        currentProgram.getDataTypeManager().resolve(frame,DataTypeConflictHandler.REPLACE_HANDLER);
        String[][] labels=ru ? new String[][] {
            {"1050:0000","animation_table_allocate"},{"1050:0054","animation_next_native"},
            {"1050:00df","animation_get_clamped_native"},{"1050:0142","animation_write_stream"},
            {"1050:026b","animation_read_stream"},{"1068:0421","object_animation_step"},
            {"1038:0265","event_set_repeat_interval"}
        } : new String[][] {
            {"1018:0f0e","animation_table_allocate"},{"1018:0f62","animation_next_native"},
            {"1018:0fed","animation_get_clamped_native"},{"1018:1050","animation_write_stream"},
            {"1018:11b1","animation_read_stream"},{"1020:0901","object_animation_step"}
        };
        File dir=new File(getScriptArgs()[1]); dir.mkdirs();
        DecompInterface dec=new DecompInterface(); dec.openProgram(currentProgram);
        try(PrintWriter c=new PrintWriter(Files.newBufferedWriter(new File(dir,"annotated.c").toPath(),StandardCharsets.UTF_8));
            PrintWriter asm=new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8));
            PrintWriter index=new PrintWriter(Files.newBufferedWriter(new File(dir,"roles.tsv").toPath(),StandardCharsets.UTF_8))) {
            index.println("address\trole");
            for(String[] row:labels) {
                Function f=getFunctionAt(toAddr(row[0]));
                if(f==null) throw new IOException("No known entry "+row[0]);
                f.setName(row[1],SourceType.USER_DEFINED);
                f.setComment("Stage 3 evidence: docs/animations.md. Types and original calling signature remain provisional.");
                index.println(row[0]+"\t"+row[1]);
                var result=dec.decompileFunction(f,60,monitor);
                if(!result.decompileCompleted()) throw new IOException(result.getErrorMessage());
                c.println("/* "+row[0]+" */"); c.println(result.getDecompiledFunction().getC());
                asm.println("FUNCTION "+row[0]+" "+row[1]);
                for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true)) asm.println(ins.getAddress()+"\t"+ins);
            }
        } finally {dec.dispose();}
        println("KREMLIN_STAGE3_COMPLETE "+lang+" layouts=2 roles="+labels.length);
    }
}
