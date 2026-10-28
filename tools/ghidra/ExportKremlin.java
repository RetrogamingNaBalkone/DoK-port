// Ghidra headless script. Addresses and pseudocode are analysis artifacts.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class ExportKremlin extends GhidraScript {
    private PrintWriter writer(File dir, String name) throws IOException {
        return new PrintWriter(Files.newBufferedWriter(new File(dir, name).toPath(), StandardCharsets.UTF_8));
    }
    public void run() throws Exception {
        File dir = new File(getScriptArgs()[0]);
        new File(dir, "functions").mkdirs();
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
                // Verified runtime entry 1000:17bb has WAIT; FXAM before its frame.
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
        // Additional evidence-based entries and labels, supplied by NE segment index.
        try (var seeds = Files.newBufferedReader(new File(getScriptArgs()[1]).toPath(), StandardCharsets.UTF_8)) {
            String line;
            while ((line=seeds.readLine())!=null) {
                if (line.isBlank() || line.startsWith("#")) continue;
                String[] fields=line.split("\t");
                int index=Integer.parseInt(fields[0]);
                MemoryBlock block=currentProgram.getMemory().getBlock("Code"+index);
                if (block==null) throw new IOException("Missing Code"+index);
                var address=block.getStart().add(Long.parseLong(fields[1],16));
                Function f=getFunctionAt(address);
                if (f==null && getFunctionContaining(address)==null) { disassemble(address); f=createFunction(address,null); }
                if (f!=null) {
                    f.setName(fields[2],ghidra.program.model.symbol.SourceType.USER_DEFINED);
                    f.setComment("Evidence-based role from WASDify; signature and types remain provisional. "+fields[3]);
                } else throw new IOException("Seed is not an original function entry: "+line);
            }
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
        // Ghidra's standard C exporter also supplies referenced globals and type definitions.
        var exporter=new ghidra.app.util.exporter.CppExporter();
        if(!exporter.export(new File(dir,"with_types.c"),currentProgram,null,monitor)) throw new IOException("C exporter failed");
        println("KREMLIN_EXPORT_COMPLETE "+currentProgram.getName()+" "+success+"/"+total);
    }
}
