import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
/** G12 evidence. Read existing analysis, preserve project and original bytes. */
public class InspectItems extends GhidraScript {
 public void run() throws Exception {
  boolean ru=getScriptArgs()[0].equals("ru");File dir=new File(getScriptArgs()[1]);dir.mkdirs();
  String segment=ru?"1068":"1020";
  DecompInterface dec=new DecompInterface();dec.openProgram(currentProgram);
  try(PrintWriter asm=new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8));
      PrintWriter c=new PrintWriter(Files.newBufferedWriter(new File(dir,"automatic.c").toPath(),StandardCharsets.UTF_8))) {
   /* Complete actor segment preserves the virtual-call context and readers;
    * automatic C is evidence only and never linked into the engine. */
   for(Function f:currentProgram.getFunctionManager().getFunctions(true)) {
    if(!f.getEntryPoint().toString().startsWith(segment+":")) continue;
    asm.println("FUNCTION "+f.getEntryPoint()+" "+f.getName());
    for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true)) asm.println(ins.getAddress()+"\t"+ins);
    c.println("/* "+f.getEntryPoint()+" unverified automatic pseudocode */");
    var result=dec.decompileFunction(f,60,monitor);if(result.decompileCompleted()) c.println(result.getDecompiledFunction().getC());
   }
  } finally {dec.dispose();}
  Address start=toAddr(segment+":0000");MemoryBlock block=currentProgram.getMemory().getBlock(start);
  byte[] b=new byte[(int)Math.min(65536,block.getEnd().subtract(start)+1)];
  currentProgram.getMemory().getBytes(start,b);Files.write(new File(dir,"actor.bin").toPath(),b);
  println("KREMLIN_ITEMS_COMPLETE");
 }
}
