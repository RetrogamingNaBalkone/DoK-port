import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

/* G10 evidence only. Automatic C remains separate from engine modules. */
public class InspectSurfaces extends GhidraScript {
 public void run() throws Exception {
  boolean ru=getScriptArgs()[0].equals("ru"); File dir=new File(getScriptArgs()[1]);dir.mkdirs();
  Set<Function> functions=new LinkedHashSet<>();
  String[] entries=ru?new String[]{"1098:30c1","1098:5435","10b0:1007","10b0:157e","10b0:15f4","10b0:1ab6","1120:0b12","1120:1074","1088:0544","1010:0000","1010:00d2"}
   :new String[]{"1030:3571","1030:6136","1040:102b","1040:15ae","1040:1624","1040:1af2","1078:0b18","1078:107a","1028:196e","1010:0000","1010:00d2"};
  for(String e:entries) functions.add(getFunctionAt(toAddr(e)));
  /* Export all small surface accessors, and callers writing the light-near global. */
  String actor=ru?"1068":"1020", data=ru?"1190":"10d0";
  for(Function f:currentProgram.getFunctionManager().getFunctions(true)) {
   if(f.getEntryPoint().toString().startsWith(actor+":") && f.getBody().getNumAddresses()<160) functions.add(f);
  }
  Address near=toAddr(data+":"+(ru?"0dfa":"0ea2"));
  for(var it=currentProgram.getReferenceManager().getReferencesTo(near);it.hasNext();) {
   Function f=getFunctionContaining(it.next().getFromAddress()); if(f!=null) functions.add(f);
  }
  DecompInterface dec=new DecompInterface();dec.openProgram(currentProgram);
  try(PrintWriter asm=new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8));
      PrintWriter c=new PrintWriter(Files.newBufferedWriter(new File(dir,"automatic.c").toPath(),StandardCharsets.UTF_8))) {
   for(Function f:functions) {
    if(f==null) throw new IOException("Missing entry");
    asm.println("FUNCTION "+f.getEntryPoint()+" "+f.getName());
    for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true)) asm.println(ins.getAddress()+"\t"+ins);
    c.println("/* "+f.getEntryPoint()+" unverified automatic pseudocode */");
    var result=dec.decompileFunction(f,90,monitor);if(result.decompileCompleted()) c.println(result.getDecompiledFunction().getC());
   }
  } finally {dec.dispose();}
  String[] bases=ru?new String[]{"10b0:0000","1120:0000","1088:0000","1010:0000","1068:0000","1190:0000","10f8:0000"}
   :new String[]{"1040:0000","1078:0000","1028:0000","1010:0000","1020:0000","10d0:0000","1060:0000"};
  String[] names={"geometry","raster","light","span","actor","data","image"};
  for(int k=0;k<bases.length;k++) {
   Address start=toAddr(bases[k]);MemoryBlock block=currentProgram.getMemory().getBlock(start);
   byte[] b=new byte[(int)Math.min(65536,block.getEnd().subtract(start)+1)];
   currentProgram.getMemory().getBytes(start,b);Files.write(new File(dir,names[k]+".bin").toPath(),b);
  }
  println("KREMLIN_SURFACES_COMPLETE");
 }
}
