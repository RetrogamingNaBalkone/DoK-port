// Add only evidence-backed labels and partial native layouts; never infer host pointers.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class AnnotateKremlin extends GhidraScript {
    private final CategoryPath category = new CategoryPath("/DoK/Native16");
    private StructureDataType layout(String name, int size) {
        StructureDataType s = new StructureDataType(category, name, size);
        s.setPackingEnabled(false);
        return s;
    }
    private void field(StructureDataType s, int offset, DataType type, String name) {
        s.replaceAtOffset(offset, type, type.getLength(), name, "Verified offset; see docs/native-state.md and docs/resources.md");
    }
    private void save(DataType type) {
        currentProgram.getDataTypeManager().resolve(type, DataTypeConflictHandler.REPLACE_HANDLER);
    }
    public void run() throws Exception {
        String lang=getScriptArgs()[0];
        boolean ru=lang.equals("ru");
        if (!ru && !lang.equals("en")) throw new IOException("Expected ru or en");
        File dir=new File(getScriptArgs()[1]); dir.mkdirs();
        StructureDataType far=layout("Far16Token",4);
        field(far,0,WordDataType.dataType,"offset"); field(far,2,WordDataType.dataType,"selector"); save(far);
        StructureDataType res=layout("ResEntry30",30);
        field(res,0,new ArrayDataType(ByteDataType.dataType,16,1),"name_cp866");
        field(res,16,DWordDataType.dataType,"payload_size"); field(res,20,DWordDataType.dataType,"file_offset");
        field(res,24,WordDataType.dataType,"runtime_cache_flags"); field(res,26,far,"runtime_cached_pointer"); save(res);
        StructureDataType manager=layout("ResourceManagerPartial12",12);
        field(manager,0,WordDataType.dataType,"count"); field(manager,2,far,"entries"); save(manager);
        StructureDataType sprite=layout("IndexedImage18",18);
        field(sprite,0,WordDataType.dataType,"width"); field(sprite,2,WordDataType.dataType,"height");
        field(sprite,4,WordDataType.dataType,"flags"); field(sprite,6,WordDataType.dataType,"crop_divisor");
        field(sprite,8,WordDataType.dataType,"crop_index"); field(sprite,10,DWordDataType.dataType,"pixel_bytes");
        field(sprite,14,far,"runtime_pixels"); save(sprite);
        StructureDataType event=layout("Event16",16);
        field(event,0,DWordDataType.dataType,"deadline"); field(event,4,DWordDataType.dataType,"interval");
        field(event,8,far,"callback"); field(event,12,DWordDataType.dataType,"parameter"); save(event);
        StructureDataType owner=layout("EventOwner16",16);
        field(owner,0,WordDataType.dataType,"count"); field(owner,2,WordDataType.dataType,"capacity");
        field(owner,4,far,"events"); field(owner,8,far,"next"); field(owner,12,far,"previous"); save(owner);
        StructureDataType player=layout("PlayerPartial222",222);
        field(player,16,WordDataType.dataType,"vtable_near_offset");
        field(player,18,WordDataType.dataType,"native_type");
        field(player,0x26,DWordDataType.dataType,"x_bits"); field(player,0x2a,DWordDataType.dataType,"y_bits");
        field(player,0x2e,DWordDataType.dataType,"z_bits"); field(player,0x32,ShortDataType.dataType,"heading");
        field(player,0x5e,ShortDataType.dataType,"health"); field(player,0x60,ShortDataType.dataType,"energy");
        field(player,0x62,ShortDataType.dataType,"health_max"); field(player,0x64,ShortDataType.dataType,"energy_max");
        field(player,0x72,WordDataType.dataType,"keys");
        field(player,0xaa,new ArrayDataType(far,9,4),"weapons");
        field(player,0xce,ShortDataType.dataType,"selected_weapon"); field(player,0xd0,ShortDataType.dataType,"pending_weapon");
        field(player,0xd6,ShortDataType.dataType,"lives"); field(player,0xd8,ShortDataType.dataType,"score"); save(player);
        String[][] labels=ru ? new String[][] {
            {"10e0:0000","resource_manager_open"},{"10e0:0105","resource_cache_clear"},
            {"10e0:0251","resource_find_index"},{"10e0:03f2","resource_open_stream"},
            {"10e0:046b","resource_load_cached"},{"10f8:0000","image_allocate"},
            {"10f8:02d9","image_read_stream"},{"1038:0000","event_owner_initialize"},
            {"1038:0108","event_schedule"},{"1068:0000","object_factory"},
            {"1068:1657","player_initialize"},{"1048:004c","level_parse_text"}
        } : new String[][] {
            {"1058:0c02","resource_manager_open"},{"1058:0d0d","resource_cache_clear"},
            {"1058:0e6b","resource_find_index"},{"1058:100c","resource_open_stream"},
            {"1058:1085","resource_load_cached"},{"1010:13aa","event_owner_initialize"},
            {"1010:14be","event_schedule"},{"1020:018c","object_factory"},
            {"1020:1b61","player_initialize"}
        };
        DecompInterface dec=new DecompInterface(); dec.openProgram(currentProgram);
        try(PrintWriter c=new PrintWriter(Files.newBufferedWriter(new File(dir,"annotated.c").toPath(),StandardCharsets.UTF_8));
            PrintWriter asm=new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8));
            PrintWriter index=new PrintWriter(Files.newBufferedWriter(new File(dir,"roles.tsv").toPath(),StandardCharsets.UTF_8))) {
            index.println("address\trole");
            for(String[] label:labels) {
                var address=toAddr(label[0]); Function f=getFunctionAt(address);
                if(f==null) throw new IOException("Not a known function: "+label[0]);
                f.setName(label[1],SourceType.USER_DEFINED);
                f.setComment("Stage 2: evidence and limitations in docs/resources.md / docs/native-state.md. Inferred signature remains unverified.");
                index.println(label[0]+"\t"+label[1]);
                c.println("/* "+label[0]+" */");
                var result=dec.decompileFunction(f,60,monitor);
                if(!result.decompileCompleted()) throw new IOException(result.getErrorMessage());
                c.println(result.getDecompiledFunction().getC());
                asm.println("FUNCTION "+label[0]+" "+label[1]);
                for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true)) asm.println(ins.getAddress()+"\t"+ins);
            }
        } finally {dec.dispose();}
        println("KREMLIN_STAGE2_COMPLETE "+lang+" layouts=7 roles="+labels.length);
    }
}
