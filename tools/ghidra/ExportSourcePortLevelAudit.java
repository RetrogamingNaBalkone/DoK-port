// Read-only SP04 evidence export from the existing OldGold project.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class ExportSourcePortLevelAudit extends GhidraScript {
    public void run() throws Exception {
        File dir = new File(getScriptArgs()[0]); dir.mkdirs();
        int[] tables = {0x6ca,0x642,0x5ca,0x5ae,0x596,0x65a,0x612,0x5fa,0x62a,0x5e2,0x57a,0,0x69e,0x672};
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(new File(dir,"vtable-readers.tsv").toPath(), StandardCharsets.UTF_8))) {
            out.println("class\tvtable_ghidra\treader_ghidra");
            for (int c=0;c<tables.length;c++) if(tables[c]!=0) {
                var a=toAddr(String.format("3255:%04x",tables[c]+4));
                int off=getShort(a)&65535, seg=getShort(a.add(2))&65535;
                out.printf("%d\t3255:%04x\t%04x:%04x%n",c,tables[c],seg,off);
            }
        }
        String[] entries={"1a0c:005c","1ae1:0003","1ae1:0477","1ae1:0699","1ae1:073d","1ae1:1236","1ae1:13ef","1ae1:21ea","1ae1:2719","1ae1:2a6d","1ae1:2c2e","1ae1:3a42","1000:56f2","1000:5a08","1000:5c7c","1000:55a6","1000:5eaf","1ae1:3c77","1ae1:128f","1ae1:3cba","1ae1:3cfd","1ae1:3d41","1ae1:0829","1ae1:2b18","1ae1:2b78","1ae1:1fed","1ae1:3178","1ae1:3d84","1ae1:059c","1ae1:06fa"};
        String[] extra={"1ae1:420d","1000:5d58","1000:5e34","1a0c:0730","1a0c:07fe","1a0c:084c","1a0c:08c6","1a0c:0938"};
        String[] all=new String[entries.length+extra.length];System.arraycopy(entries,0,all,0,entries.length);System.arraycopy(extra,0,all,entries.length,extra.length);
        try(PrintWriter out = new PrintWriter(Files.newBufferedWriter(new File(dir,"readers.asm").toPath(),StandardCharsets.UTF_8))) {
            for(String entry:all){
                Function f=getFunctionAt(toAddr(entry)); if(f==null)throw new IOException("Missing function "+entry);
                out.println("\nFUNCTION "+entry+" "+f.getName());
                InstructionIterator it=currentProgram.getListing().getInstructions(f.getBody(),true);
                while(it.hasNext()){Instruction ins=it.next();out.println(ins.getAddress()+"\t"+ins);}
            }
        }
        println("SOURCE_PORT_LEVEL_AUDIT_COMPLETE");
    }
}
