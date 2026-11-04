// Transient disassembly and original-instruction evidence, read-only project.
//@category DoK.SourcePort
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import java.nio.file.*;
public class ExportSourcePortMonsters extends GhidraScript {
 public void run() throws Exception {
  String[] a=getScriptArgs(); Path out=Path.of(a[0]); Files.createDirectories(out);
  if(!a[1].equals(currentProgram.getExecutableSHA256()))throw new Exception("Provenance mismatch");
  String profile=a[2];
  String[] functions=profile.equals("old-gold")?new String[]{"1ae1:1694","1ae1:320a","1ae1:32ac","1ae1:3323","1ae1:3989"}:
    profile.equals("ru")?new String[]{"1068:3747","1068:37cf","1068:3845","1068:3eca"}:
    new String[]{"1020:3eb0","1020:3f38","1020:3fae","1020:4633"};
  DecompInterface dec=new DecompInterface();dec.openProgram(currentProgram);
  StringBuilder auto=new StringBuilder("/* Unverified automatic pseudocode; never buildable engine C. */\n");
  for(String addr:functions){var start=toAddr(addr);var f=getFunctionAt(start);
   if(f==null){disassemble(start);f=createFunction(start,null);}
   auto.append("/* ").append(addr).append(" */\n");
   if(f!=null){var result=dec.decompileFunction(f,60,monitor);if(result.decompileCompleted())auto.append(result.getDecompiledFunction().getC());}
  }
  dec.dispose();Files.writeString(out.resolve("automatic.c"),auto);
  for(int i=3;i<a.length;i+=3){var start=toAddr(a[i+1]);int length=Integer.decode(a[i+2]);byte[] bytes=new byte[length];
   currentProgram.getMemory().getBytes(start,bytes);Files.write(out.resolve(a[i]+".bin"),bytes);
   StringBuilder s=new StringBuilder();var it=currentProgram.getListing().getInstructions(start,true);
   while(it.hasNext()){var ins=it.next();if(ins.getAddress().subtract(start)>=length)break;s.append(ins.getAddress()).append('\t').append(ins).append('\n');}
   Files.writeString(out.resolve(a[i]+".asm"),s);
  }
  println("SOURCE_PORT_MONSTERS_EXPORTED");
 }
}
