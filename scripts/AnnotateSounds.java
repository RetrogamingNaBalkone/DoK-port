import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class AnnotateSounds extends GhidraScript {
    public void run() throws Exception {
        String lang=getScriptArgs()[0]; boolean ru=lang.equals("ru");
        if(!ru && !lang.equals("en")) throw new IOException("Expected ru or en");
        CategoryPath category=new CategoryPath("/DoK/Native16");
        DataType far=currentProgram.getDataTypeManager().getDataType(category,"Far16Token");
        if(far==null || far.getLength()!=4) throw new IOException("Run stage 2 first");
        StructureDataType s=new StructureDataType(category,"SoundHeader36",36);
        s.setPackingEnabled(false);
        s.replaceAtOffset(0,WordDataType.dataType,2,"native_vtable_value","Skipped when binding a resource; not a disk magic");
        s.replaceAtOffset(2,new ArrayDataType(ByteDataType.dataType,3,1),3,"signature_svm",null);
        s.replaceAtOffset(5,WordDataType.dataType,2,"version",null);
        s.replaceAtOffset(7,ByteDataType.dataType,1,"flags","Disk 09h; runtime 20h borrowed, 40h repeat, 80h playing");
        s.replaceAtOffset(8,WordDataType.dataType,2,"sample_bits",null);
        s.replaceAtOffset(10,WordDataType.dataType,2,"format_aux","Zero in every original; other formats unverified");
        s.replaceAtOffset(12,WordDataType.dataType,2,"parameter_c","Observed 255; meaning not confirmed");
        s.replaceAtOffset(14,WordDataType.dataType,2,"parameter_d","Observed 128; meaning not confirmed");
        s.replaceAtOffset(16,DWordDataType.dataType,4,"sample_rate","Backend programs its low 16 bits");
        s.replaceAtOffset(20,DWordDataType.dataType,4,"sample_bytes",null);
        s.replaceAtOffset(24,far,4,"runtime_samples","Null disk pointer bound to base+36; host decoder ignores pointer");
        currentProgram.getDataTypeManager().resolve(s,DataTypeConflictHandler.REPLACE_HANDLER);
        String[][] labels=ru ? new String[][] {
            {"1108:0000","sound_initialize"},{"1108:00ec","sound_bind_resource"},
            {"10e8:007c","sound_read_stream"},{"1108:01f7","sound_play_once"},
            {"1108:0232","sound_play_loop"},{"1108:026d","sound_stop"},
            {"1108:02b9","sound_is_playing"},{"1118:046e","sound_dma_fill_buffer"},
            {"1118:0829","sound_dma_begin"},{"1118:0222","sound_dsp_dma_program"}
        } : new String[][] {
            {"1060:153a","sound_initialize"},{"1060:1632","sound_bind_resource"},
            {"1058:1254","sound_read_stream"},{"1060:173d","sound_play_once"},
            {"1060:1778","sound_play_loop"},{"1060:17b3","sound_stop"},
            {"1060:17ff","sound_is_playing"},{"1070:046e","sound_dma_fill_buffer"},
            {"1070:083d","sound_dma_begin"},{"1070:0222","sound_dsp_dma_program"}
        };
        File dir=new File(getScriptArgs()[1]); dir.mkdirs();
        DecompInterface dec=new DecompInterface(); dec.openProgram(currentProgram);
        try(PrintWriter c=new PrintWriter(Files.newBufferedWriter(new File(dir,"annotated.c").toPath(),StandardCharsets.UTF_8));
            PrintWriter asm=new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8));
            PrintWriter index=new PrintWriter(Files.newBufferedWriter(new File(dir,"roles.tsv").toPath(),StandardCharsets.UTF_8))) {
            index.println("address\trole");
            for(String[] label:labels) {
                Function f=getFunctionAt(toAddr(label[0]));
                if(f==null) throw new IOException("Missing known entry "+label[0]);
                f.setName(label[1],SourceType.USER_DEFINED);
                f.setComment("Stage 4 evidence: docs/sound.md. Native signature remains provisional.");
                index.println(label[0]+"\t"+label[1]);
                var result=dec.decompileFunction(f,60,monitor);
                if(!result.decompileCompleted()) throw new IOException(result.getErrorMessage());
                c.println("/* "+label[0]+" */"); c.println(result.getDecompiledFunction().getC());
                asm.println("FUNCTION "+label[0]+" "+label[1]);
                for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true)) asm.println(ins.getAddress()+"\t"+ins);
            }
        } finally {dec.dispose();}
        println("KREMLIN_STAGE4_COMPLETE "+lang+" layouts=1 roles="+labels.length);
    }
}
