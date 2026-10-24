import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class AnnotateWorld extends GhidraScript {
    public void run() throws Exception {
        String lang=getScriptArgs()[0]; boolean ru=lang.equals("ru");
        if(!ru && !lang.equals("en")) throw new IOException("Expected ru or en");
        CategoryPath category=new CategoryPath("/DoK/Native16");
        DataType far=currentProgram.getDataTypeManager().getDataType(category,"Far16Token");
        if(far==null || far.getLength()!=4) throw new IOException("Run stage 2 first");
        StructureDataType s=new StructureDataType(category,"WorldHeaderPartial1866",0x74a);
        s.setPackingEnabled(false);
        s.replaceAtOffset(0,ShortDataType.dataType,2,"width",null);
        s.replaceAtOffset(2,ShortDataType.dataType,2,"height",null);
        s.replaceAtOffset(4,ShortDataType.dataType,2,"length",null);
        s.replaceAtOffset(0x30e,new ArrayDataType(far,256,4),1024,"objects_by_glyph",null);
        s.replaceAtOffset(0x70e,WordDataType.dataType,2,"cell_count",null);
        s.replaceAtOffset(0x710,ShortDataType.dataType,2,"pickup_99_total",null);
        s.replaceAtOffset(0x734,far,4,"player",null);
        s.replaceAtOffset(0x738,ShortDataType.dataType,2,"cell_side",null);
        s.replaceAtOffset(0x73a,ShortDataType.dataType,2,"cell_height",null);
        s.replaceAtOffset(0x73c,WordDataType.dataType,2,"slot_allocation_count",null);
        s.replaceAtOffset(0x73e,DWordDataType.dataType,4,"stride_x",null);
        s.replaceAtOffset(0x742,DWordDataType.dataType,4,"stride_z",null);
        currentProgram.getDataTypeManager().resolve(s,DataTypeConflictHandler.REPLACE_HANDLER);
        java.util.List<String[]> labels=new java.util.ArrayList<>();
        for(String line:Files.readAllLines(new File(getScriptArgs()[1],"requested-roles.tsv").toPath(),StandardCharsets.UTF_8)) {
            if(!line.startsWith("address")) labels.add(line.split("\\t"));
        }
        File dir=new File(getScriptArgs()[1]); dir.mkdirs();
        DecompInterface dec=new DecompInterface(); dec.openProgram(currentProgram);
        try(PrintWriter c=new PrintWriter(Files.newBufferedWriter(new File(dir,"annotated.c").toPath(),StandardCharsets.UTF_8));
            PrintWriter asm=new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8));
            PrintWriter index=new PrintWriter(Files.newBufferedWriter(new File(dir,"roles.tsv").toPath(),StandardCharsets.UTF_8))) {
            index.println("address\trole");
            for(String[] label:labels) {
                Function f=getFunctionAt(toAddr(label[0]));
                if(f==null) { disassemble(toAddr(label[0])); f=createFunction(toAddr(label[0]),label[1]); }
                if(f==null) throw new IOException("Missing verified entry "+label[0]);
                f.setName(label[1],SourceType.USER_DEFINED);
                f.setComment("Stage 6 evidence: docs/world.md. Native signature remains provisional.");
                index.println(label[0]+"\t"+label[1]);
                var result=dec.decompileFunction(f,60,monitor);
                if(!result.decompileCompleted()) throw new IOException(result.getErrorMessage());
                c.println("/* "+label[0]+" */"); c.println(result.getDecompiledFunction().getC());
                asm.println("FUNCTION "+label[0]+" "+label[1]);
                for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true)) asm.println(ins.getAddress()+"\t"+ins);
            }
        } finally {dec.dispose();}
        println("KREMLIN_STAGE6_COMPLETE "+lang+" layouts=1 roles="+labels.size());
    }
}
