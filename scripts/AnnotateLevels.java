import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class AnnotateLevels extends GhidraScript {
    public void run() throws Exception {
        String lang=getScriptArgs()[0]; boolean ru=lang.equals("ru");
        if(!ru && !lang.equals("en")) throw new IOException("Expected ru or en");
        CategoryPath category=new CategoryPath("/DoK/Native16");
        DataType far=currentProgram.getDataTypeManager().getDataType(category,"Far16Token");
        if(far==null || far.getLength()!=4) throw new IOException("Run stage 2 first");
        StructureDataType s=new StructureDataType(category,"LevelObjectDescriptor22",22);
        s.setPackingEnabled(false);
        s.replaceAtOffset(0,WordDataType.dataType,2,"class_id",null);
        s.replaceAtOffset(2,ByteDataType.dataType,1,"glyph_cp866",null);
        s.replaceAtOffset(3,ShortDataType.dataType,2,"width_parameter",null);
        s.replaceAtOffset(5,ShortDataType.dataType,2,"height_parameter",null);
        s.replaceAtOffset(7,far,4,"sprite",null);
        s.replaceAtOffset(11,far,4,"animation",null);
        s.replaceAtOffset(15,far,4,"sound",null);
        s.replaceAtOffset(19,WordDataType.dataType,2,"flags",null);
        s.replaceAtOffset(21,ByteDataType.dataType,1,"marker",null);
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
                f.setComment("Stage 5 evidence: docs/levels.md. Native signature remains provisional.");
                index.println(label[0]+"\t"+label[1]);
                var result=dec.decompileFunction(f,60,monitor);
                if(!result.decompileCompleted()) throw new IOException(result.getErrorMessage());
                c.println("/* "+label[0]+" */"); c.println(result.getDecompiledFunction().getC());
                asm.println("FUNCTION "+label[0]+" "+label[1]);
                for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true)) asm.println(ins.getAddress()+"\t"+ins);
            }
        } finally {dec.dispose();}
        println("KREMLIN_STAGE5_COMPLETE "+lang+" layouts=1 roles="+labels.size());
    }
}
