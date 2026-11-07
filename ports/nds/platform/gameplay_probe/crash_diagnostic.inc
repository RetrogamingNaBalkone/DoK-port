/* ARM9-only breadcrumbs. No filesystem, IPC, allocation or per-event flush.
 * Written from the main loop only; frozen before fatal-screen reconfiguration. */
#if NDS_EMBEDDED_RESOURCES
#include "diagnostic_build.inc"
#else
#define DOK_DIAGNOSTIC_BUILD "external-probe"
#endif
enum {
    DIAG_BOOT, DIAG_FRAME, DIAG_AUDIO, DIAG_INPUT, DIAG_SIMULATION,
    DIAG_SPRITES, DIAG_TEXTURE, DIAG_EFFECTS, DIAG_PLAN, DIAG_SUBMIT,
    DIAG_WAIT, DIAG_UPLOAD, DIAG_MAP, DIAG_GPU, DIAG_DEADLINE, DIAG_CLOCK
};
static const char *const diagnostic_names[]={
    "boot","frame","audio","input","simulation","sprites","texture",
    "effects","plan","submit","wait","upload","map","GPU","deadline","clock"
};
typedef struct {uint32_t frame,ticks,operation,a,b;} DiagnosticEvent;
typedef struct {
    uint32_t count,frozen,status_valid,operation;
    int32_t status;
    uint32_t a,b;
    DiagnosticEvent events[8];
} CrashDiagnostic;
static CrashDiagnostic crash_diagnostic;
static const char *failure_function,*failure_file;
static uint32_t failure_line;
static void diagnostic_event(uint32_t operation,uint32_t a,uint32_t b)
{
    if(crash_diagnostic.frozen)return;
    DiagnosticEvent *e=&crash_diagnostic.events[crash_diagnostic.count++&7u];
    *e=(DiagnosticEvent){obs.frame_id,cpuGetTiming(),operation,a,b};
}
static int gameplay_failure_at(const char *function,const char *file,uint32_t line)
{
    if(!failure_line){
        failure_function=function;failure_file=file;failure_line=line;
        crash_diagnostic.frozen=1;
        if(!crash_diagnostic.status_valid && crash_diagnostic.count){
            const DiagnosticEvent *e=&crash_diagnostic.events[(crash_diagnostic.count-1u)&7u];
            crash_diagnostic.operation=e->operation;
            crash_diagnostic.a=e->a;crash_diagnostic.b=e->b;
        }
    }
    return 0;
}
#define GAMEPLAY_FAILURE() gameplay_failure_at(__func__,__FILE__,__LINE__)
static int diagnostic_check(uint32_t operation,int32_t status,int32_t expected,
    uint32_t a,uint32_t b,const char *function,const char *file,uint32_t line)
{
    if(status==expected)return 1;
    if(!failure_line){
        diagnostic_event(operation,a,b);
        crash_diagnostic.status_valid=1;crash_diagnostic.status=status;
        crash_diagnostic.operation=operation;crash_diagnostic.a=a;crash_diagnostic.b=b;
        gameplay_failure_at(function,file,line);
    }
    return 0;
}
/* Each argument is evaluated once. Status belongs to the named operation's API. */
#define DIAG_CHECK(op,call,ok,a,b) \
    ({int32_t dok_diag_result_=(call); \
      diagnostic_check(op,dok_diag_result_,ok,a,b,__func__,__FILE__,__LINE__);})
static const char *diagnostic_name(uint32_t operation)
{
    return operation<sizeof(diagnostic_names)/sizeof(*diagnostic_names)?diagnostic_names[operation]:"unknown";
}
static const char *diagnostic_basename(const char *file)
{
    const char *result=file?file:"unknown";
    if(file)for(const char *p=file;*p;++p)if(*p=='/'||*p=='\\')result=p+1;
    return result;
}
static void diagnostic_history_screen(void)
{
    static PrintConsole history_console;
    videoSetModeSub(MODE_0_2D);vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&history_console,0,BgType_Text4bpp,BgSize_T_256x256,31,0,false,true);
    consoleSelect(&history_console);
    printf("Last events: oldest -> newest\n");
    uint32_t count=crash_diagnostic.count<8u?crash_diagnostic.count:8u;
    for(uint32_t i=0;i<count;++i){
        const DiagnosticEvent *e=&crash_diagnostic.events[(crash_diagnostic.count-count+i)&7u];
        printf("F%lu %s T%08lX\n %08lX %08lX\n",(unsigned long)e->frame,
            diagnostic_name(e->operation),(unsigned long)e->ticks,(unsigned long)e->a,(unsigned long)e->b);
    }
    printf("A/B: operation parameters (hex)\nT: ARM9 hardware timer (hex)\nPhotograph BOTH screens.\nCold restart to play again.\n");
}
