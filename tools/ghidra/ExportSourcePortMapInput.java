import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;

// Read-only session export of the original keyboard IRQ, edge helper and map input.
public class ExportSourcePortMapInput extends GhidraScript {
 public void run() throws Exception {
  boolean ru=getScriptArgs()[0].equals("ru");
  Path out=Paths.get(getScriptArgs()[1]);Files.createDirectories(out);
  String keyboard=ru?"1070":"1028",input=ru?"10c0":"1050";
  for(String seg:new String[]{keyboard,input}){
   Address start=toAddr(seg+":0000");var block=currentProgram.getMemory().getBlock(start);
   byte[] bytes=new byte[(int)Math.min(65536,block.getEnd().subtract(start)+1)];
   currentProgram.getMemory().getBytes(start,bytes);Files.write(out.resolve(seg+".bin"),bytes);
  }
  Address start=toAddr(keyboard+(ru?":0109":":0115")),end=toAddr(keyboard+(ru?":01ea":":01f6"));
  disassemble(start);
  try(PrintWriter asm=new PrintWriter(Files.newBufferedWriter(out.resolve("keyboard-irq.asm"),StandardCharsets.UTF_8))){
   for(Instruction ins:currentProgram.getListing().getInstructions(start,true)){
    if(ins.getAddress().compareTo(end)>0)break;
    asm.println(ins.getAddress()+"\t"+ins);
   }
  }
  println("MAP_INPUT_EXPORT_COMPLETE");
 }
}
