import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
/** N17 far copy helper ABI, exported read-only for actual instruction replay. */
public class ExportNdsCompiler extends GhidraScript {
 public void run() throws Exception {
  boolean ru=getScriptArgs()[0].equals("ru");Path out=Path.of(getScriptArgs()[1]);Files.createDirectories(out);
  String expected=ru?"12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28":"cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b";
  if(!expected.equals(currentProgram.getExecutableSHA256()))throw new IllegalStateException("Program SHA mismatch");
  var start=toAddr("1000:0000");var block=currentProgram.getMemory().getBlock(start);byte[] b=new byte[(int)block.getSize()];currentProgram.getMemory().getBytes(start,b);Files.write(out.resolve("1000.bin"),b);
  StringBuilder asm=new StringBuilder();var end=toAddr("1000:5cf5");
  for(Instruction ins:currentProgram.getListing().getInstructions(toAddr("1000:5cb3"),true)){
   if(ins.getAddress().compareTo(end)>0)break;asm.append(ins.getAddress()).append('\t').append(ins).append('\n');}
  Files.writeString(out.resolve("evidence.asm"),asm,StandardCharsets.UTF_8);
  println("NDS_COMPILER_EXPORT_COMPLETE");
 }
}
