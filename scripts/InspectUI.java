import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
/** G17 font/HUD/menu/notification evidence. Automatic C is never engine source. */
public class InspectUI extends GhidraScript {
 public void run() throws Exception {
  boolean ru=getScriptArgs()[0].equals("ru");File dir=new File(getScriptArgs()[1]);dir.mkdirs();
  String[] segments=ru?new String[]{"1008","1058","1068","10c0"}:new String[]{"1008","1018","1020","1050"};
  DecompInterface dec=new DecompInterface();dec.openProgram(currentProgram);
  try(PrintWriter asm=new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8));
      PrintWriter c=new PrintWriter(Files.newBufferedWriter(new File(dir,"automatic.c").toPath(),StandardCharsets.UTF_8))) {
   for(Function f:currentProgram.getFunctionManager().getFunctions(true)) {
    boolean selected=false;for(String seg:segments)if(f.getEntryPoint().toString().startsWith(seg+":"))selected=true;if(!selected)continue;
    asm.println("FUNCTION "+f.getEntryPoint()+" "+f.getName());
    for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true)) asm.println(ins.getAddress()+"\t"+ins);
    c.println("/* "+f.getEntryPoint()+" unverified automatic pseudocode */");
    var result=dec.decompileFunction(f,60,monitor);if(result.decompileCompleted())c.println(result.getDecompiledFunction().getC());
   }
  } finally {dec.dispose();}
  String[] binaries=ru?new String[]{"1190","1058","10c0"}:new String[]{"10d0","1018","1050"};
  for(String seg:binaries){Address start=toAddr(seg+":0000");MemoryBlock block=currentProgram.getMemory().getBlock(start);byte[] b=new byte[(int)Math.min(65536,block.getEnd().subtract(start)+1)];currentProgram.getMemory().getBytes(start,b);Files.write(new File(dir,seg+".bin").toPath(),b);}
  println("KREMLIN_UI_COMPLETE");
 }
}
