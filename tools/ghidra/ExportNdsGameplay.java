import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.PrintWriter;
/** Read-only N14-N17 source evidence. Never compile automatic.c as engine C. */
public class ExportNdsGameplay extends GhidraScript {
 public void run() throws Exception {
  boolean ru=getScriptArgs()[0].equals("ru");Path out=Path.of(getScriptArgs()[1]);Files.createDirectories(out);
  String expected=ru?"12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28":"cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b";
  if(!expected.equals(currentProgram.getExecutableSHA256()))throw new IllegalStateException("Program SHA mismatch");
  String[] segments=ru?new String[]{"1068","1098","10c8"}:new String[]{"1020","1030","1058"};
  String[] names={"actor","world","math"};
  DecompInterface dec=new DecompInterface();dec.openProgram(currentProgram);
  try(PrintWriter asm=new PrintWriter(Files.newBufferedWriter(out.resolve("evidence.asm"),StandardCharsets.UTF_8));
      PrintWriter c=new PrintWriter(Files.newBufferedWriter(out.resolve("automatic.c"),StandardCharsets.UTF_8))) {
   for(int k=0;k<segments.length;++k){
    var start=toAddr(segments[k]+":0000");var block=currentProgram.getMemory().getBlock(start);
    byte[] b=new byte[(int)block.getSize()];currentProgram.getMemory().getBytes(start,b);Files.write(out.resolve(names[k]+".bin"),b);
    for(Function f:currentProgram.getFunctionManager().getFunctions(true)){
     if(!f.getEntryPoint().toString().startsWith(segments[k]+":"))continue;
     asm.println("FUNCTION "+f.getEntryPoint()+" "+f.getName());
     for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true))asm.println(ins.getAddress()+"\t"+ins);
     c.println("/* "+f.getEntryPoint()+" automatic, ABI unverified */");
     var result=dec.decompileFunction(f,30,monitor);if(result.decompileCompleted())c.println(result.getDecompiledFunction().getC());
    }
   }
  }finally{dec.dispose();}
  Files.writeString(out.resolve("program-source.tsv"),currentProgram.getName()+"\t"+expected+"\n",StandardCharsets.UTF_8);
  println("NDS_GAMEPLAY_EXPORT_COMPLETE");
 }
}
