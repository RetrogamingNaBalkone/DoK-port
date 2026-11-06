// Read-only original-instruction export; no analysis or patching.
//@category DoK.SourcePort
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class ExportSourcePortProjectiles extends GhidraScript {
 public void run() throws Exception {
  String[] a=getScriptArgs(); Path out=Path.of(a[0]); Files.createDirectories(out);
  if(!a[1].equals(currentProgram.getExecutableSHA256()))throw new Exception("Ghidra sample provenance differs");
  for(int i=2;i<a.length;i+=3){
   var start=toAddr(a[i+1]);
   if(a[i].equals("reclaim-flag")){disassemble(start);if(getFunctionAt(start)==null)createFunction(start,null);}
   int length=Integer.decode(a[i+2]); byte[] bytes=new byte[length];
   if(currentProgram.getMemory().getBytes(start,bytes)!=length)throw new Exception("Short read");
   Files.write(out.resolve(a[i]+".bin"),bytes);
   StringBuilder s=new StringBuilder();var it=currentProgram.getListing().getInstructions(start,true);
   while(it.hasNext()){var ins=it.next();if(ins.getAddress().subtract(start)>=length)break;s.append(ins.getAddress()).append('\t').append(ins).append('\n');}
   Files.writeString(out.resolve(a[i]+".asm"),s);
  }
  println("SOURCE_PORT_SCHEDULER_EXPORTED");
 }
}
