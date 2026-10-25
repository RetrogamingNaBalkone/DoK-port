import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/* Targeted G09 evidence. Automatic pseudocode is not engine source. */
public class InspectInteractions extends GhidraScript {
 public void run() throws Exception {
  boolean ru=getScriptArgs()[0].equals("ru");
  File dir=new File(getScriptArgs()[1]);dir.mkdirs();
  for(String a:ru?new String[]{"1068:4297","1068:0850"}:new String[]{"1020:4e94","1020:0d42"}) disassemble(toAddr(a));
  String[] ranges=ru?new String[]{"1068:0000","1068:4451","1098:0309","1098:0447","10f8:0592","10f8:09e0"}
   :new String[]{"1020:0000","1020:50b8","1030:03ed","1030:052b","1060:06a2","1060:0af0"};
  DecompInterface dec=new DecompInterface();dec.openProgram(currentProgram);
  try(PrintWriter asm=new PrintWriter(Files.newBufferedWriter(new File(dir,"evidence.asm").toPath(),StandardCharsets.UTF_8));
      PrintWriter c=new PrintWriter(Files.newBufferedWriter(new File(dir,"automatic.c").toPath(),StandardCharsets.UTF_8))) {
   for(int k=0;k<ranges.length;k+=2) {
    var start=toAddr(ranges[k]);var end=toAddr(ranges[k+1]);
    for(Instruction ins:currentProgram.getListing().getInstructions(start,true)) {
     if(ins.getAddress().compareTo(end)>0) break;
     asm.println(ins.getAddress()+"\t"+ins);
    }
    for(Function f:currentProgram.getFunctionManager().getFunctions(start,true)) {
     if(f.getEntryPoint().compareTo(end)>0) break;
     c.println("/* "+f.getEntryPoint()+" automatic, signature unverified */");
     var result=dec.decompileFunction(f,60,monitor);
     if(result.decompileCompleted()) c.println(result.getDecompiledFunction().getC());
    }
   }
  } finally {dec.dispose();}
  String[] bases=ru?new String[]{"1068:0000","1190:0000"}:new String[]{"1020:0000","10d0:0000"};
  for(int k=0;k<2;k++) {
   byte[] b=new byte[k==0?(ru?0x4452:0x50b9):0x1000];
   if(currentProgram.getMemory().getBytes(toAddr(bases[k]),b)!=b.length) throw new IOException("Partial export");
   try(FileOutputStream out=new FileOutputStream(new File(dir,k==0?"actor.bin":"data.bin"))) {out.write(b);}
  }
  byte[] crop=new byte[ru?0xa00:0xb10];
  if(currentProgram.getMemory().getBytes(toAddr(ru?"10f8:0000":"1060:0000"),crop)!=crop.length) throw new IOException("Partial crop export");
  try(FileOutputStream out=new FileOutputStream(new File(dir,"image.bin"))) {out.write(crop);}
  println("KREMLIN_INTERACTIONS_COMPLETE");
 }
}
