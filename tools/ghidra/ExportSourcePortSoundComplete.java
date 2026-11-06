// Read-only export including the previously unrecognised SB DMA IRQ entries.
//@category DoK.SourcePort
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class ExportSourcePortSoundComplete extends GhidraScript {
 public void run() throws Exception {
  String[] a=getScriptArgs();Path out=Path.of(a[0]);Files.createDirectories(out);
  if(!a[1].equals(currentProgram.getExecutableSHA256()))throw new Exception("Sample SHA differs");
  String name=currentProgram.getName().toUpperCase();
  String[] entries=name.equals("PKR.EXE")?new String[]{"1118:08a9","1118:08bf"}:
    name.equals("PKE.EXE")?new String[]{"1070:08bd","1070:08d3"}:new String[]{};
  if(entries.length>0){var first=toAddr(entries[0]);clearListing(first,first.add(0x1f3));}
  for(String entry:entries){var address=toAddr(entry);println("DISASM "+entry+" "+disassemble(address));if(getFunctionAt(address)==null)createFunction(address,null);}
  for(int i=2;i<a.length;i+=3){var start=toAddr(a[i+1]);int length=Integer.decode(a[i+2]);byte[] bytes=new byte[length];
   if(currentProgram.getMemory().getBytes(start,bytes)!=length)throw new Exception("Short read");
   Files.write(out.resolve(a[i]+".bin"),bytes);StringBuilder text=new StringBuilder();var it=currentProgram.getListing().getInstructions(start,true);
   while(it.hasNext()){var ins=it.next();if(ins.getAddress().subtract(start)>=length)break;text.append(ins.getAddress()).append('\t').append(ins).append('\n');}
   Files.writeString(out.resolve(a[i]+".asm"),text);
  }
  println("SOURCE_PORT_SCHEDULER_EXPORTED");
 }
}
