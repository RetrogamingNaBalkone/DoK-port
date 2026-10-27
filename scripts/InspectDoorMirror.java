import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;

/* Read-only targeted evidence. Addresses use the Ghidra NE loader. */
public class InspectDoorMirror extends GhidraScript {
 public void run() throws Exception {
  boolean ru=getScriptArgs()[0].equals("ru");
  Path out=Paths.get(getScriptArgs()[1]);Files.createDirectories(out);
  String base=ru?"10b0:0000":"1040:0000";
  byte[] code=new byte[ru?0x55a:0x566];
  if(currentProgram.getMemory().getBytes(toAddr(base),code)!=code.length)throw new IOException("Partial export");
  Files.write(out.resolve("center.bin"),code);
  String[] ranges=ru?new String[]{"1098:4600","1098:4f80","10b0:0000","10b0:05b2"}
                   :new String[]{"1030:4f00","1030:5b10","1040:0000","1040:05be"};
  try(PrintWriter f=new PrintWriter(Files.newBufferedWriter(out.resolve("evidence.asm"),StandardCharsets.UTF_8))){
   for(int i=0;i<ranges.length;i+=2)for(Instruction ins:currentProgram.getListing().getInstructions(toAddr(ranges[i]),true)){
    if(ins.getAddress().compareTo(toAddr(ranges[i+1]))>0)break;
    f.println(ins.getAddress()+"\t"+ins);
   }
  }
  println("DOOR_MIRROR_COMPLETE");
 }
}
