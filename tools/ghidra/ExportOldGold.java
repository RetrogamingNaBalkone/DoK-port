// Ghidra headless script. Addresses and pseudocode are analysis artifacts.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class ExportOldGold extends GhidraScript {
    private PrintWriter writer(File dir, String name) throws IOException {
        return new PrintWriter(Files.newBufferedWriter(new File(dir, name).toPath(), StandardCharsets.UTF_8));
    }
    public void run() throws Exception {
        File dir = new File(getScriptArgs()[0]);
        File functionsDirectory = new File(dir, "functions");
        functionsDirectory.mkdirs();
        // Remove only this exporter's generated function files before a complete export.
        for (File old : functionsDirectory.listFiles()) {
            if (old.isFile() && old.getName().matches("[0-9a-fA-F]+_[0-9a-fA-F]+\\.c"))
                Files.delete(old.toPath());
        }
        int discovered = 0, rejected = 0;
        // Provenance: WASDify GhidraPkrPerformance.java, Borland method prologue.
        // Candidate boundaries are only accepted when disassembly/function creation succeeds.
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (!b.isExecute() || !b.isInitialized()) continue;
            byte[] bytes = new byte[(int)b.getSize()];
            b.getBytes(b.getStart(), bytes);
            for (int i=0; i+7<=bytes.length; i++) {
                boolean prologue = (bytes[i]&255)==0x8c && (bytes[i+1]&255)==0xd0
                    && (bytes[i+2]&255)==0x90 && (bytes[i+3]&255)==0x45
                    && (bytes[i+4]&255)==0x55 && (bytes[i+5]&255)==0x8b && (bytes[i+6]&255)==0xec;
                // Ordinary far routines use INC BP; PUSH BP; MOV BP,SP.
                // Near routines use PUSH BP; MOV BP,SP. Keep INC BP as entry.
                boolean farFrame = (bytes[i]&255)==0x45 && (bytes[i+1]&255)==0x55
                    && (bytes[i+2]&255)==0x8b && (bytes[i+3]&255)==0xec;
                boolean nearFrame = (bytes[i]&255)==0x55 && (bytes[i+1]&255)==0x8b
                    && (bytes[i+2]&255)==0xec && (i==0 || (bytes[i-1]&255)!=0x45);
                // Accept the x87 prefix before its stack frame (shared Borland runtime idiom).
                boolean x87Frame = (bytes[i]&255)==0x9b && (bytes[i+1]&255)==0xd9
                    && (bytes[i+2]&255)==0xe5 && (bytes[i+3]&255)==0x55
                    && (bytes[i+4]&255)==0x8b && (bytes[i+5]&255)==0xec;
                if (nearFrame && i>=3 && (bytes[i-3]&255)==0x9b
                    && (bytes[i-2]&255)==0xd9 && (bytes[i-1]&255)==0xe5) nearFrame=false;
                if (!prologue && !farFrame && !nearFrame && !x87Frame) continue;
                var a=b.getStart().add(i);
                if (getFunctionAt(a)!=null) continue;
                if (getFunctionContaining(a)!=null) { rejected++; continue; }
                // A stack-frame setup can occur after an actual entry (e.g. x87 FXAM).
                // Do not split a known instruction flow merely because it matches a prologue.
                var previous=currentProgram.getListing().getInstructionBefore(a);
                if (previous!=null && a.equals(previous.getFallThrough())) { rejected++; continue; }
                disassemble(a);
                Function candidate = createFunction(a, null);
                if (candidate!=null) {
                    candidate.setComment("Heuristic compiler-prologue discovery; boundary/signature need review.");
                    discovered++;
                } else rejected++;
            }
        }
        // Optional manually verified Ghidra MZ addresses, never NE indices.
        File seedFile = new File(getScriptArgs()[1]);
        for (String line : Files.readAllLines(seedFile.toPath(), StandardCharsets.UTF_8)) {
            if (line.isBlank() || line.startsWith("#")) continue;
            String[] fields = line.split("\t", 3);
            var address = toAddr(fields[0]);
            Function f = getFunctionAt(address);
            if (f == null && getFunctionContaining(address) == null) {
                disassemble(address);
                f = createFunction(address, null);
            }
            if (f == null) throw new IOException("Seed is not a function entry: " + line);
            f.setName(fields[1], ghidra.program.model.symbol.SourceType.USER_DEFINED);
            f.setComment("Old Gold role; provisional signature. Evidence: " + fields[2]);
        }
        analyzeChanges(currentProgram);
        DecompInterface dec=new DecompInterface();
        if (!dec.openProgram(currentProgram)) throw new IOException(dec.getLastMessage());
        int total=0, success=0;
        try (PrintWriter c=writer(dir,"decompiled.c"); PrintWriter listing=writer(dir,"listing.asm");
             PrintWriter index=writer(dir,"functions.tsv"); PrintWriter blocks=writer(dir,"blocks.tsv");
             PrintWriter refs=writer(dir,"references.tsv"); PrintWriter gaps=writer(dir,"unrecovered.tsv")) {
            c.println("/* Ghidra analysis pseudocode, NOT buildable or behavior-verified source. */");
            c.println("/* Segmented pointers, inferred types and register arguments require manual recovery. */");
            index.println("address\tname\tbody_bytes\tstatus\tdiagnostic\tfile");
            blocks.println("name\tstart\tend\tsize\texecute\tinitialized\tinstruction_bytes\tfunction_bytes");
            gaps.println("block\tstart\tend\tkind");
            for (MemoryBlock b:currentProgram.getMemory().getBlocks()) {
                long insBytes=0;
                for (Instruction ins:currentProgram.getListing().getInstructions(new ghidra.program.model.address.AddressSet(b.getStart(),b.getEnd()),true)) insBytes+=ins.getLength();
                var functionSet=new ghidra.program.model.address.AddressSet();
                for (Function f:currentProgram.getFunctionManager().getFunctions(true)) functionSet.add(f.getBody());
                long funBytes=functionSet.intersectRange(b.getStart(),b.getEnd()).getNumAddresses();
                blocks.printf("%s\t%s\t%s\t%d\t%b\t%b\t%d\t%d%n",b.getName(),b.getStart(),b.getEnd(),b.getSize(),b.isExecute(),b.isInitialized(),insBytes,funBytes);
                if (b.isExecute() && b.isInitialized()) {
                    var unowned=new ghidra.program.model.address.AddressSet(b.getStart(),b.getEnd()).subtract(functionSet);
                    for (var range:unowned.getAddressRanges()) gaps.printf("%s\t%s\t%s\toutside_function_body_may_be_data_or_padding%n",b.getName(),range.getMinAddress(),range.getMaxAddress());
                }
            }
            for (Instruction ins:currentProgram.getListing().getInstructions(true)) {
                listing.println(ins.getAddress()+"\t"+ins);
                for (var ref:ins.getReferencesFrom()) refs.printf("%s\t%s\t%s%n",ins.getAddress(),ref.getToAddress(),ref.getReferenceType());
            }
            for (Function f:currentProgram.getFunctionManager().getFunctions(true)) {
                monitor.checkCancelled(); total++;
                var result=dec.decompileFunction(f,60,monitor);
                String filename="functions/"+f.getEntryPoint().toString().replace(':','_')+".c";
                String diagnostic=result.getErrorMessage()==null?"":result.getErrorMessage().replace('\t',' ').replace('\n',' ');
                boolean ok=result.decompileCompleted() && result.getDecompiledFunction()!=null;
                index.printf("%s\t%s\t%d\t%s\t%s\t%s%n",f.getEntryPoint(),f.getName(),f.getBody().getNumAddresses(),ok?"ok":"failed",diagnostic,ok?filename:"");
                c.println("\n/* FUNCTION "+f.getEntryPoint()+" "+f.getName()+" */");
                if (ok) {
                    success++;
                    String code=result.getDecompiledFunction().getC(); c.println(code);
                    try(PrintWriter one=writer(dir,filename)) { one.println("/* "+currentProgram.getName()+" "+f.getEntryPoint()+" */"); one.print(code); }
                } else c.println("/* DECOMPILATION FAILED: "+diagnostic.replace("*/","* /")+" */");
            }
        } finally { dec.dispose(); }
        try(PrintWriter summary=writer(dir,"summary.txt")) {
            summary.println("program="+currentProgram.getName());
            summary.println("language="+currentProgram.getLanguageID());
            summary.println("compiler="+currentProgram.getCompilerSpec().getCompilerSpecID());
            summary.println("functions="+total); summary.println("decompiled="+success);
            summary.println("failed="+(total-success)); summary.println("prologue_functions_added="+discovered);
            summary.println("prologue_candidates_rejected="+rejected);
        }
        // Raw terminated strings, with physical EXE offsets for independent checking.
        // Data can live in blocks marked executable by the DOS loader.
        try (PrintWriter strings = writer(dir, "strings.tsv")) {
            strings.println("address\tfile_offset\ttext_cp866");
            var charset = java.nio.charset.Charset.forName("IBM866");
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                if (!b.isInitialized() || b.getName().equals("HEADER")) continue;
                byte[] data = new byte[(int)b.getSize()];
                b.getBytes(b.getStart(), data);
                for (int i = 0; i < data.length;) {
                    int start = i;
                    while (i < data.length && ((data[i] & 255) >= 32) && data[i] != 127) i++;
                    if (i - start >= 4 && i < data.length && data[i] == 0) {
                        var a = b.getStart().add(start);
                        long fileOffset = 0x2600L + a.getOffset() - 0x10000L;
                        String value = new String(data, start, i-start, charset).replace('\t',' ');
                        strings.printf("%s\t0x%x\t%s%n", a, fileOffset, value);
                    }
                    i++;
                }
            }
        }
        // Ghidra's standard C exporter also supplies referenced globals and type definitions.
        var exporter=new ghidra.app.util.exporter.CppExporter();
        if(!exporter.export(new File(dir,"with_types.c"),currentProgram,null,monitor)) throw new IOException("C exporter failed");
        println("OLD_GOLD_EXPORT_COMPLETE "+currentProgram.getName()+" "+success+"/"+total);
    }
}
