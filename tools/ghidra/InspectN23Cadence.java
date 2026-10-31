import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
/** Read-only original PKR NE timing evidence; no engine code is generated. */
public class InspectN23Cadence extends GhidraScript {
 public void run() throws Exception {
  File dir=new File(getScriptArgs()[0]);dir.mkdirs();
  String[] ranges={"1008:0d6a","1008:0ffc","1038:0108","1038:0445",
    "1050:0000","1050:01a0","1068:3262","1068:3747","1068:3845","1068:3eca",
    "10c0:0ba1","10c0:1390","1130:015d","1130:02b6"};
  try(PrintWriter out=new PrintWriter(Files.newBufferedWriter(new File(dir,"timing.asm").toPath(),StandardCharsets.UTF_8))){
   for(int n=0;n<ranges.length;n+=2){
    var start=toAddr(ranges[n]);var end=toAddr(ranges[n+1]);
    out.println("RANGE "+start+".."+end);
    for(Instruction ins:currentProgram.getListing().getInstructions(start,true)){
     if(ins.getAddress().compareTo(end)>0)break;out.println(ins.getAddress()+"\t"+ins);
    }
   }
  }
  for(String seg:new String[]{"1008","1038","1050","1068","10c0","1130","1190"}){
   var start=toAddr(seg+":0000");var block=currentProgram.getMemory().getBlock(start);
   byte[] bytes=new byte[(int)Math.min(65536,block.getEnd().subtract(start)+1)];
   currentProgram.getMemory().getBytes(start,bytes);Files.write(new File(dir,seg+".bin").toPath(),bytes);
  }
  println("N23_ORIGINAL_CADENCE_COMPLETE");
 }
}
