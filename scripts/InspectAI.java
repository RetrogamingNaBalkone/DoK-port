import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
/** G14 evidence. Read existing analysis, preserve project and original bytes. */
public class InspectAI extends GhidraScript {
 public void run() throws Exception {
  boolean ru=getScriptArgs()[0].equals("ru");File dir=new File(getScriptArgs()[1]);dir.mkdirs();
  String segment=ru?"1068":"1020";String world=ru?"1098":"1030"; String math=ru?"10c8":"1058"; String tables=ru?"1180":"10c0";
  DecompInterface dec=new DecompInterface();dec.openProgram(currentProgram);
  try(PrintWriter asm=new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8));
      PrintWriter c=new PrintWriter(Files.newBufferedWriter(new File(dir,"automatic.c").toPath(),StandardCharsets.UTF_8))) {
   /* Complete actor segment preserves the virtual-call context and readers;
    * automatic C is evidence only and never linked into the engine. */
   for(Function f:currentProgram.getFunctionManager().getFunctions(true)) {
    if(!f.getEntryPoint().toString().startsWith(segment+":") && !f.getEntryPoint().toString().startsWith(world+":") && !f.getEntryPoint().toString().startsWith(math+":")) continue;
    asm.println("FUNCTION "+f.getEntryPoint()+" "+f.getName());
    for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true)) asm.println(ins.getAddress()+"\t"+ins);
    c.println("/* "+f.getEntryPoint()+" unverified automatic pseudocode */");
    var result=dec.decompileFunction(f,60,monitor);if(result.decompileCompleted()) c.println(result.getDecompiledFunction().getC());
   }
  } finally {dec.dispose();}
  Address start=toAddr(segment+":0000");MemoryBlock block=currentProgram.getMemory().getBlock(start);
  byte[] b=new byte[(int)Math.min(65536,block.getEnd().subtract(start)+1)];
  currentProgram.getMemory().getBytes(start,b);Files.write(new File(dir,"actor.bin").toPath(),b);
  start=toAddr(world+":0000");block=currentProgram.getMemory().getBlock(start);
  b=new byte[(int)Math.min(65536,block.getEnd().subtract(start)+1)];
  currentProgram.getMemory().getBytes(start,b);Files.write(new File(dir,"world.bin").toPath(),b);
  start=toAddr(math+":0000");block=currentProgram.getMemory().getBlock(start); b=new byte[(int)Math.min(65536,block.getEnd().subtract(start)+1)]; currentProgram.getMemory().getBytes(start,b);Files.write(new File(dir,"math.bin").toPath(),b); start=toAddr(tables+":0b48");b=new byte[202]; currentProgram.getMemory().getBytes(start,b);Files.write(new File(dir,"atan.bin").toPath(),b); start=toAddr((ru?"1190:060d":"10d0:0616"));b=new byte[64];currentProgram.getMemory().getBytes(start,b);Files.write(new File(dir,"vtable.bin").toPath(),b); println("KREMLIN_AI_COMPLETE");
 }
}


