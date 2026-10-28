import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/* Targeted rendering evidence. Auto-C is diagnostic, never engine source. */
public class InspectRendering extends GhidraScript {
    private PrintWriter writer(File dir,String name) throws IOException {
        return new PrintWriter(Files.newBufferedWriter(new File(dir,name).toPath(),StandardCharsets.UTF_8));
    }
    public void run() throws Exception {
        String lang=getScriptArgs()[0];boolean ru=lang.equals("ru");
        if(!ru && !lang.equals("en")) throw new IOException("Expected ru/en");
        File dir=new File(getScriptArgs()[1]);dir.mkdirs();
        String[][] roles={
            {"1100:00f5","1060:1025","render_camera_initialize"},
            {"1100:0000","1060:0f30","render_sort_descending_depth"},
            {"1100:0301","1060:128f","render_column_visibility"},
            {"1100:0521","1060:14af","render_draw_sorted_list"},
            {"1098:30c1","1030:3571","world_build_render_geometry"},
            {"1098:5435","1030:6136","world_draw_render_list"},
            {"10b0:0000","1040:0000","render_center_vertical_plane"},
            {"10b0:05b2","1040:05be","render_center_vertical_draw"},
            {"10b0:06aa","1040:06b6","render_edge_vertical_plane"},
            {"10b0:0bf5","1040:0c0d","render_edge_vertical_draw"},
            {"10b0:0ccb","1040:0ce3","render_billboard_project"},
            {"10b0:0fb7","1040:0fdb","render_billboard_draw"},
            {"10b0:1007","1040:102b","render_textured_horizontal_quad"},
            {"10b0:157e","1040:15ae","render_textured_horizontal_draw"},
            {"10b0:15f4","1040:1624","render_solid_horizontal_quad"},
            {"10b0:1ab6","1040:1af2","render_solid_horizontal_draw"},
            {"1120:0b12","1078:0b18","rasterize_horizontal"},
            {"1120:1074","1078:107a","rasterize_vertical"},
            {"1010:0000","1010:0000","render_horizontal_span_asm"},
            {"1010:00d2","1010:00d2","render_vertical_span_asm"},
            {"1088:0544","1028:196e","render_light_row_select"},
            {"1088:0498","1028:18c2","render_light_table_read"},
            {"1088:0168","1028:14ea","render_light_table_initialize"},
            {"10f8:0000","1060:0000","image_allocate"},
            {"10f8:02d9","1060:034d","image_read_stream"},
            {"10f8:0592","1060:06a2","image_flags_crop_configure"},
            {"10f8:05c9","1060:06d9","image_crop_transform"},
            {"1050:026b","1018:11b1","animation_read_stream"},
            {"1008:0719","1008:0aa9","startup_render_configuration"},
            {"10c8:0000","1058:0000","render_trig_initialize"},
            {"1000:1cc7","1000:1cc7","runtime_fpu_to_i32"},
            {"10b0:055a","1040:0566","render_center_horizontal_bounds"},
            {"10b0:0b9d","1040:0bb5","render_edge_horizontal_bounds"},
            {"10b0:0f5f","1040:0f83","render_billboard_horizontal_bounds"}
        };
        /* ENG main entry is located by its verified Ghidra function name below
         * if a future export changes this provisional candidate. */
        DecompInterface dec=new DecompInterface();dec.openProgram(currentProgram);
        try(PrintWriter c=writer(dir,"annotated.c");PrintWriter asm=writer(dir,"evidence.asm");
            PrintWriter index=writer(dir,"roles.tsv");PrintWriter callers=writer(dir,"callers.tsv")) {
            index.println("address\trole\tbytes");callers.println("callee\tcaller\tname");
            for(String[] role:roles) {
                String address=role[ru?0:1];Function f=getFunctionAt(toAddr(address));
                if(f==null && role[2].equals("startup_render_configuration")) {
                    println("SKIP provisional startup candidate "+address);continue;
                }
                if(f==null) throw new IOException("Missing verified entry "+address);
                f.setName(role[2],SourceType.USER_DEFINED);
                f.setComment("G04 targeted rendering evidence. Signature/segment semantics require ASM review; see docs/originals/rendering-geometry.md.");
                index.println(address+"\t"+role[2]+"\t"+f.getBody().getNumAddresses());
                for(Function caller:f.getCallingFunctions(monitor)) callers.println(address+"\t"+caller.getEntryPoint()+"\t"+caller.getName());
                var result=dec.decompileFunction(f,90,monitor);
                if(!result.decompileCompleted()) throw new IOException(address+": "+result.getErrorMessage());
                c.println("/* "+address+" "+role[2]+" - unverified automatic pseudocode */");
                c.println(result.getDecompiledFunction().getC());
                asm.println("FUNCTION "+address+" "+role[2]);
                for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true)) asm.println(ins.getAddress()+"\t"+ins);
            }
        } finally {dec.dispose();}
        String data=ru?"1190":"10d0";
        String trig=ru?"1180":"10c0";
        try(PrintWriter tables=writer(dir,"trig-tables.tsv");PrintWriter globals=writer(dir,"static-data.tsv");
            PrintWriter constants=writer(dir,"static-math-constants.tsv")) {
            constants.println("address\tbits64\tfloat32\tdouble64");
            for(int offset:(ru?new int[]{0xab8,0xac0,0xac4,0xac8}:new int[]{0xb6c,0xb74,0xb78,0xb7c})) {
                var address=toAddr(data+":"+Integer.toHexString(offset));
                long bits=currentProgram.getMemory().getLong(address);
                constants.println(address+"\t"+Long.toUnsignedString(bits)+"\t"+Float.intBitsToFloat((int)bits)+"\t"+Double.longBitsToDouble(bits));
            }
            tables.println("# Static executable BSS; runtime_trig_initialize fills this table. Not runtime sin/cos values.");
            tables.println("angle\ttable0\ttable5a4");
            for(int n=0;n<=360;++n) tables.println(n+"\t"+currentProgram.getMemory().getInt(toAddr(trig+":"+Integer.toHexString(n*4)))+"\t"+
                currentProgram.getMemory().getInt(toAddr(trig+":"+Integer.toHexString(0x5a4+n*4))));
            globals.println("address\tu16\tu32");
            for(int offset:new int[]{0x934,0xa44,0xaa16,0xaa32,0xaa3e,0xaa42,0xaac6,0xaae2,0xaaee,0xaaf2})
                globals.println(data+":"+Integer.toHexString(offset)+"\t"+(currentProgram.getMemory().getShort(toAddr(data+":"+Integer.toHexString(offset)))&65535)+"\t"+
                    Integer.toUnsignedString(currentProgram.getMemory().getInt(toAddr(data+":"+Integer.toHexString(offset)))));
        }
        println("KREMLIN_RENDERING_COMPLETE "+lang+" roles="+roles.length);
    }
}
