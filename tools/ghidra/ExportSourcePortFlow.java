// Read-only SP16 flow and retry evidence; automatic pseudocode is not engine C.
//@category DoK.SourcePort
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import java.nio.file.*;
public class ExportSourcePortFlow extends GhidraScript {
 public void run() throws Exception {
  String[] a=getScriptArgs();Path out=Path.of(a[0]);Files.createDirectories(out);
  if(!a[1].equals(currentProgram.getExecutableSHA256()))throw new Exception("Provenance mismatch");
  String[] functions=a[2].equals("old-gold")?new String[]{"1ae1:15a2","1ae1:1694","21b8:10e6"}:
   a[2].equals("ru")?new String[]{"1068:193e","1098:125b"}:new String[]{"1020:1efc","1030:133f"};
  DecompInterface dec=new DecompInterface();dec.openProgram(currentProgram);
  StringBuilder auto=new StringBuilder("/* Unverified automatic pseudocode; not buildable engine C. */\n");
  for(String address:functions){var start=toAddr(address);var function=getFunctionAt(start);
   if(function==null){disassemble(start);function=createFunction(start,null);}
   auto.append("/* ").append(address).append(" */\n");
   if(function!=null){var result=dec.decompileFunction(function,60,monitor);if(result.decompileCompleted())auto.append(result.getDecompiledFunction().getC());}
  }dec.dispose();Files.writeString(out.resolve("automatic.c"),auto);
  for(int i=3;i<a.length;i+=3){var start=toAddr(a[i+1]);int length=Integer.decode(a[i+2]);byte[] bytes=new byte[length];
   currentProgram.getMemory().getBytes(start,bytes);Files.write(out.resolve(a[i]+".bin"),bytes);
   StringBuilder text=new StringBuilder();var instructions=currentProgram.getListing().getInstructions(start,true);
   while(instructions.hasNext()){var instruction=instructions.next();if(instruction.getAddress().subtract(start)>=length)break;text.append(instruction.getAddress()).append('\t').append(instruction).append('\n');}
   Files.writeString(out.resolve(a[i]+".asm"),text);
  }println("SOURCE_PORT_FLOW_EXPORTED");
 }
}
