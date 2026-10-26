import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.HexFormat;

/** Reproducible loader/block evidence; no original file is modified. */
public class InspectOldGold extends GhidraScript {
 public void run() throws Exception {
  File dir = new File(getScriptArgs()[0]); dir.mkdirs();
  try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(new File(dir,"memory.tsv").toPath(), StandardCharsets.UTF_8));
       PrintWriter asm = new PrintWriter(Files.newBufferedWriter(new File(dir,"listing.asm").toPath(), StandardCharsets.UTF_8))) {
   out.println("language\t"+currentProgram.getLanguageID());
   out.println("format\t"+currentProgram.getExecutableFormat());
   out.println("block\tstart\tend\texecute\tinitialized\tsha256");
   for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
    String hash="-";
    if(b.isInitialized()) {
     byte[] data=new byte[(int)b.getSize()];currentProgram.getMemory().getBytes(b.getStart(),data);
     hash=HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(data));
    }
    out.println(b.getName()+"\t"+b.getStart()+"\t"+b.getEnd()+"\t"+b.isExecute()+"\t"+b.isInitialized()+"\t"+hash);
   }
   out.println("functions\t"+currentProgram.getFunctionManager().getFunctionCount());
   var entries=currentProgram.getSymbolTable().getExternalEntryPointIterator();
   while(entries.hasNext())out.println("entry\t"+entries.next());
   for(Instruction i:currentProgram.getListing().getInstructions(true))asm.println(i.getAddress()+"\t"+i);
  }
  println("OLD_GOLD_INSPECTION_COMPLETE");
 }
}
