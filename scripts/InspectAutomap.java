import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;

/* Read-only NE-loader evidence; automatic C is research, never engine code. */
public class InspectAutomap extends GhidraScript {
 public void run() throws Exception {
  boolean ru=getScriptArgs()[0].equals("ru");Path out=Paths.get(getScriptArgs()[1]);Files.createDirectories(out);
  String code=ru?"1098":"1030",data=ru?"1190":"10d0",entry=ru?"1290":"1374";
  for(String seg:new String[]{code,data}){
   Address start=toAddr(seg+":0000");var block=currentProgram.getMemory().getBlock(start);
   byte[] bytes=new byte[(int)Math.min(65536,block.getEnd().subtract(start)+1)];
   currentProgram.getMemory().getBytes(start,bytes);Files.write(out.resolve(seg+".bin"),bytes);
  }
  Function f=getFunctionAt(toAddr(code+":"+entry));
  try(PrintWriter asm=new PrintWriter(Files.newBufferedWriter(out.resolve("automap.asm"),StandardCharsets.UTF_8))){
   for(Instruction ins:currentProgram.getListing().getInstructions(f.getBody(),true))asm.println(ins.getAddress()+"\t"+ins);
  }
  DecompInterface dec=new DecompInterface();dec.openProgram(currentProgram);
  try{Files.writeString(out.resolve("automatic.c"),dec.decompileFunction(f,60,monitor).getDecompiledFunction().getC());}finally{dec.dispose();}
  println("AUTOMAP_COMPLETE");
 }
}
