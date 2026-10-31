// SPDX-License-Identifier: Zlib
// Template initialization: Copyright (C) 2005 Michael Noland (joat)
// Copyright (C) 2005 Jason Rogers (Dovoto)
// Copyright (C) 2005-2015 Dave Murphy (WinterMute)
// Copyright (C) 2023 Antonio Nino Diaz
/* Custom BlocksDS ARM7 core. Platform initialization follows the installed
 * Zlib-licensed rom_combined/arm7/source/main.c template (BlocksDS 1.24.0).
 * No Maxmod or libnds sound FIFO: channel 0 and timers 0/1 belong here. */
#include <nds.h>
#include <string.h>
#include "dok/audio_codec.h"
#include "audio_ipc.h"
#include <nds/fifomessages.h>
#ifndef DOK_ARM7_IDLE_BIOS
#define DOK_ARM7_IDLE_BIOS 1
#endif
#ifndef DOK_ARM7_RAM_MAILBOX
#define DOK_ARM7_RAM_MAILBOX 0
#endif
static volatile NdsAudioShared *shared;
/* Lives beside ARM7 CRT0 in its reserved main-RAM load area, outside the
 * ARM9 arena. The N23 builder exports its ELF address, never a fixed guess. */
_Alignas(32) volatile uint32_t g_audio_arm7_boot[32]
    __attribute__((section(".crt0")))={0x37424f41u};
#if DOK_ARM7_RAM_MAILBOX
_Static_assert(sizeof(NdsInputMailbox)==32 && sizeof(touchPosition)==12,
    "Input mailbox cache line and SDK touch ABI");
bool __real_fifoSendDatamsg(u32,u32,u8 *);
bool __wrap_fifoSendDatamsg(u32 channel,u32 bytes,u8 *data)
{
    const FifoMessage *message=(const FifoMessage *)(const void *)data;
    if(channel==FIFO_SYSTEM && bytes==sizeof(FifoMessage) && data &&
       message->type==SYS_INPUT_MESSAGE){
        volatile NdsInputMailbox *box=(volatile NdsInputMailbox *)&g_audio_arm7_boot[24];
        uint32_t irq=enterCriticalSection(),sequence=box->sequence;
        box->sequence=sequence+1u;__asm__ volatile("" ::: "memory");
        memcpy((void *)box->touch,&message->SystemInput.touch,12);
        box->keys=message->SystemInput.keys;++box->count;
        __asm__ volatile("" ::: "memory");box->sequence=sequence+2u;
        leaveCriticalSection(irq);return true;
    }
    return __real_fifoSendDatamsg(channel,bytes,data);
}
#endif
static uint32_t remaining, quantum;
static uint32_t unit = NDS_AUDIO_PART_BYTES;
static int looping;
static uint32_t master_volume = 127;
static int8_t *ring;
static volatile bool exit_loop;
static volatile uint32_t voice_generation;
static volatile int resident_mode;
static const unsigned char *packed_source;
static uint32_t packed_bytes, resident_samples, source_position;
static unsigned char decode_scratch[1024] __attribute__((aligned(4)));
static void stop_voice(void)
{
    SCHANNEL_CR(0) = 0;
    TIMER0_CR = 0; TIMER1_CR = 0;
    REG_IF = IRQ_TIMER1;
    /* Retire the streamed title's PCM as well as its channel. A later
     * resident voice must never expose bytes borrowed from the title. */
    if (ring && !resident_mode) memset(ring, 0, NDS_AUDIO_RING_BYTES);
    ++voice_generation; resident_mode = 0;
    if (shared) {
        shared->status.playing = 0;
        shared->status.preparing = 0;
        shared->status.consumer_started = 0;
    }
}
static void schedule(void)
{
    quantum = looping || remaining > unit ? unit : remaining;
    TIMER1_CR = 0;
    TIMER1_DATA = (uint16_t)(0u - quantum);
    TIMER1_CR = TIMER_ENABLE | TIMER_CASCADE | TIMER_IRQ_REQ;
}
static void start_channel(void)
{
    uint32_t period = shared->status.timer_period;
    SCHANNEL_CR(0) = 0;
    SCHANNEL_SOURCE(0) = (uint32_t)(uintptr_t)ring;
    SCHANNEL_REPEAT_POINT(0) = 0;
    int one_shot = resident_mode && !looping;
    SCHANNEL_LENGTH(0) = one_shot ? (resident_samples + 3u) / 4u :
                                  NDS_AUDIO_RING_BYTES / 4u;
    SCHANNEL_TIMER(0) = (uint16_t)(0u - period);
    TIMER0_CR = 0;
    TIMER0_DATA = (uint16_t)(0u - 2u * period);
    REG_IF = IRQ_TIMER1;
    schedule();
    shared->status.preparing = 0;
    shared->status.consumer_started = 1;
    SCHANNEL_CR(0) = SCHANNEL_ENABLE | (one_shot ? SOUND_ONE_SHOT : SOUND_REPEAT) |
                     SOUND_FORMAT_8BIT | SOUND_VOL(master_volume) | SOUND_PAN(64);
    TIMER0_CR = TIMER_ENABLE | ClockDivider_1;
}
static void audio_irq(void)
{
    if (!shared || !shared->status.playing || !shared->status.consumer_started) return;
    ++shared->status.status_sequence;
    ++shared->status.irq_count;
    shared->status.consumed_samples += quantum;
    ++shared->status.consumed_parts;
    if (!looping) remaining -= quantum;
    if (!looping && !remaining) stop_voice();
    else {
        uint32_t next = shared->status.consumed_parts;
        uint32_t need = looping || remaining > unit ? unit : remaining;
        int ready = resident_mode ?
            shared->status.producer_samples - shared->status.consumed_samples >= need :
            shared->part_ready[next & 1u] == next;
        if (!ready) {
            ++shared->status.underruns;
            stop_voice();
        } else if (!looping && remaining < unit) {
            /* Full parts use timer1's hardware reload without rearming it in
             * the IRQ: interrupt latency must not accumulate at every part.
             * Only the final partial deadline changes. Preserve sample ticks
             * already elapsed since the previous boundary. */
            uint32_t elapsed;
            TIMER1_CR = 0;
            elapsed = (uint16_t)(TIMER1_DATA + quantum);
            quantum = remaining;
            if (elapsed >= quantum) {
                shared->status.consumed_samples += remaining;
                remaining = 0;
                stop_voice();
            } else {
                TIMER1_DATA = (uint16_t)(elapsed - quantum);
                TIMER1_CR = TIMER_ENABLE | TIMER_CASCADE | TIMER_IRQ_REQ;
            }
        }
    }
    ++shared->status.status_sequence;
}
static void retire_one_shot(void)
{
    uint32_t saved = enterCriticalSection();
    /* Hardware one-shot completion is authoritative even if a timer IRQ is
     * delayed by the emulator. Do not leave admission busy after it ended. */
    if (shared && resident_mode && !looping && shared->status.playing &&
        shared->status.consumer_started && !(SCHANNEL_CR(0) & SCHANNEL_ENABLE)) {
        ++shared->status.status_sequence;
        shared->status.consumed_samples = resident_samples;
        shared->status.consumed_parts = (resident_samples + unit - 1u) / unit;
        remaining = 0;
        stop_voice();
        ++shared->status.status_sequence;
    }
    leaveCriticalSection(saved);
}
static void receive(void *address, void *unused)
{
    ++g_audio_arm7_boot[3];g_audio_arm7_boot[4]=(uint32_t)(uintptr_t)address;
    g_audio_arm7_boot[5]=1;
    NdsAudioCommand message;
    (void)unused;
    uintptr_t at=(uintptr_t)address;
    if(at<0x02000000u || at>0x02400000u-sizeof(message) || (at&31u)) {
        g_audio_arm7_boot[5]=2;
        if (shared) ++shared->status.stale_commands;
        return;
    }
    memcpy(&message,address,sizeof(message));
    if (message.magic != NDS_AUDIO_IPC_MAGIC) {g_audio_arm7_boot[5]=3;return;}
    g_audio_arm7_boot[5]=4;
    if (message.op == NDS_AUDIO_ATTACH) {
        stop_voice();
        /* ARM9 main RAM only; reject FIFO pointer values outside DS RAM. */
        if (message.shared < 0x02000000u ||
            message.shared > 0x02400000u - sizeof(NdsAudioShared) ||
            message.ring < 0x02000000u ||
            message.ring > 0x02400000u - NDS_AUDIO_RING_BYTES ||
            (message.shared & 31u) || (message.ring & 31u)) {g_audio_arm7_boot[5]=5;return;}
        shared = (NdsAudioShared *)(uintptr_t)message.shared;
        ring = (int8_t *)(uintptr_t)message.ring;
        shared->status.ready = 1;
    }
    if (!shared) return;
    ++shared->status.status_sequence;
    if (message.op == NDS_AUDIO_PLAY || message.op == NDS_AUDIO_RESIDENT) {
        int is_resident = message.op == NDS_AUDIO_RESIDENT;
        if (shared->status.playing) ++shared->status.rejected_busy;
        else if (message.rate < 8000 || message.rate > 48000 ||
                 !message.samples || !message.identity ||
                 (!is_resident && (shared->part_ready[0] != 0 || shared->part_ready[1] != 1)) ||
                 (is_resident && (message.samples > 65536u || message.packed_bytes < 8u ||
                  message.packed_bytes > 65536u || message.packed < 0x02000000u ||
                  message.packed > 0x02400000u - message.packed_bytes)))
            ++shared->status.stale_commands;
        else {
            uint32_t period = (BUS_CLOCK / 2u + message.rate / 2u) / message.rate;
            remaining = message.samples; looping = !!message.loop;
            ++voice_generation; resident_mode = is_resident;
            unit = is_resident ? 1024u : NDS_AUDIO_PART_BYTES;
            shared->status.identity = message.identity;
            shared->status.consumed_parts = 0;
            shared->status.consumed_samples = 0;
            shared->status.timer_period = period;
            shared->status.playing = 1;
            shared->status.producer_samples = 0;
            shared->status.preparing = is_resident;
            shared->status.consumer_started = 0;
            if (is_resident) {
                packed_source = (const unsigned char *)(uintptr_t)message.packed;
                packed_bytes = message.packed_bytes; resident_samples = message.samples;
                source_position = 0;
            }
            /* One hardware clock, rounded once: sample counter and sound
             * cannot drift. EOS stops at the sample deadline (IRQ latency
             * remains hardware-dependent and is measured separately). */
            else start_channel();
        }
    } else if (message.op == NDS_AUDIO_STOP) {
        /* Identity 0 is the ARM9 lifecycle barrier (world/language change).
         * A source stop for another owner is a legal, silent no-op. */
        if (!message.identity || message.identity == shared->status.identity)
            stop_voice();
    } else if (message.op == NDS_AUDIO_VOLUME) {
        if (message.volume <= 127u) {
            master_volume = message.volume;
            SCHANNEL_CR(0) = (SCHANNEL_CR(0) & ~SOUND_VOL(127)) |
                             SOUND_VOL(master_volume);
        } else ++shared->status.stale_commands;
    } else if (message.op != NDS_AUDIO_ATTACH) ++shared->status.stale_commands;
    shared->status.command_ack = message.serial;
    g_audio_arm7_boot[5]=6;
    ++shared->status.status_sequence;
}
/* Runs outside every IRQ. Decoding into private ARM7 WRAM allows STOP or a
 * different play to interrupt safely. Only the final 1024-byte ring copy is
 * briefly critical; a generation check discards superseded decoder work. */
static int produce_resident(void)
{
    uint32_t saved, generation, produced, consumed, position, samples, bytes;
    uint32_t count = 1024u, copied = 0;
    const unsigned char *source;
    int repeat;
    saved = enterCriticalSection();
    if (!shared || !resident_mode || !shared->status.playing) {
        leaveCriticalSection(saved); return 0;
    }
    generation = voice_generation; produced = shared->status.producer_samples;
    consumed = shared->status.consumed_samples;
    position = source_position; samples = resident_samples; repeat = looping;
    source = packed_source; bytes = packed_bytes;
    if (produced - consumed > NDS_AUDIO_RING_BYTES - 1024u ||
        (!repeat && produced == samples)) {
        leaveCriticalSection(saved); return 0;
    }
    if (!repeat && count > samples - produced) count = samples - produced;
    leaveCriticalSection(saved);
    while (copied < count) {
        uint32_t take = count - copied;
        if (take > samples - position) take = samples - position;
        if (dok_audio_decode_range(source, bytes, samples, position,
                decode_scratch + copied, take) != DOK_STATUS_OK) {
            saved = enterCriticalSection();
            if (generation == voice_generation && resident_mode) {
                ++shared->status.status_sequence;
                ++shared->status.stale_commands; stop_voice();
                ++shared->status.status_sequence;
            }
            leaveCriticalSection(saved); return 0;
        }
        copied += take; position += take;
        if (position == samples && repeat) position = 0;
    }
    if (count < 1024u) memset(decode_scratch + count, 0, 1024u - count);
    saved = enterCriticalSection();
    if (generation != voice_generation || !resident_mode || !shared->status.playing) {
        leaveCriticalSection(saved); return 1;
    }
    ++shared->status.status_sequence;
    memcpy(ring + (produced & (NDS_AUDIO_RING_BYTES - 1u)), decode_scratch, 1024u);
    source_position = position;
    shared->status.producer_samples = produced + count;
    /* Short effects fit in the ring. Decode the complete one-shot before
     * enabling hardware, and bound hardware playback to that PCM (rounded
     * to a four-byte word, zero-padded above). Timer IRQs still publish the
     * sample deadline, but cannot replay an old ring tail if delayed. */
    if (shared->status.preparing && (looping ||
            shared->status.producer_samples == resident_samples)) start_channel();
    ++shared->status.status_sequence;
    leaveCriticalSection(saved);
    return 1;
}
static void power_callback(void) { exit_loop = true; }
#if DOK_ARM7_RAM_MAILBOX
/* BIOS IRQ entry has pushed r0-r3,r12,lr_irq. Capture the saved execution
 * context before the SDK switches stacks/modes. Tail branch preserves the
 * BIOS return link and the normal dispatcher; r0-r3 are BIOS-saved. */
__attribute__((naked,noinline,target("arm"))) static void audio_irq_probe(void)
{
    __asm__ volatile(
        "ldr r0, =g_audio_arm7_boot\n"
        "ldr r1, [sp, #20]\n"
        "str r1, [r0, #52]\n"
        "ldr r1, [sp, #12]\n"
        "str r1, [r0, #56]\n"
        "mrs r1, spsr\n"
        "str r1, [r0, #60]\n"
        "b IntrMain\n");
}
#endif
static void receive_mailbox(void)
{
#if DOK_ARM7_RAM_MAILBOX
    static uint32_t seen_serial;
    const volatile NdsAudioControl *control=(const volatile NdsAudioControl *)&g_audio_arm7_boot[16];
    uint32_t serial=control->serial;
    if(control->magic==NDS_AUDIO_CONTROL_MAGIC && serial && serial!=seen_serial){
        uint32_t address=control->address;
        /* ARM9 holds all command/control bytes immutable until ACK, or
         * forever after timeout. ARM7 has no main-RAM data cache. */
        __asm__ volatile("" ::: "memory");
        uint32_t irq=enterCriticalSection();
        seen_serial=serial;receive((void *)(uintptr_t)address,NULL);
        leaveCriticalSection(irq);
    }
#else
    (void)fifoCheckAddress(FIFO_USER_01);
#endif
}
static void vblank(void)
{
    ++g_audio_arm7_boot[11];g_audio_arm7_boot[12]=1;
    inputGetAndSend();g_audio_arm7_boot[12]=2;
}
int main(void)
{
    g_audio_arm7_boot[1]=1;
    enableSound();g_audio_arm7_boot[1]=2;
    readUserSettings();g_audio_arm7_boot[1]=3;
    ledBlink(LED_ALWAYS_ON);touchInit();g_audio_arm7_boot[1]=4;
    irqInit();
#if DOK_ARM7_RAM_MAILBOX
    IRQ_HANDLER=audio_irq_probe;
#endif
    g_audio_arm7_boot[1]=5;
    g_audio_arm7_boot[10]=fifoInit();g_audio_arm7_boot[1]=6;
    installSystemFIFO();g_audio_arm7_boot[1]=7;
    if (isDSiMode()) installCameraFIFO();
#if !DOK_ARM7_RAM_MAILBOX
    fifoSetAddressHandler(FIFO_USER_01, receive, 0);
#endif
    g_audio_arm7_boot[1]=8;
    irqSet(IRQ_TIMER1, audio_irq); irqEnable(IRQ_TIMER1);
    setPowerButtonCB(power_callback);
    initClockIRQTimer(LIBNDS_DEFAULT_TIMER_RTC);
    irqSet(IRQ_VBLANK, vblank); irqEnable(IRQ_VBLANK);
    g_audio_arm7_boot[1]=9;
    while (!exit_loop) {
        ++g_audio_arm7_boot[2];
        g_audio_arm7_boot[6]=REG_IPC_FIFO_CR;
        g_audio_arm7_boot[7]=REG_IE;g_audio_arm7_boot[8]=REG_IME;
        /* SDK polling drains and dispatches pending messages even if the
         * emulator doesn't deliver the receive interrupt. */
        g_audio_arm7_boot[9]=1;
        receive_mailbox();
        retire_one_shot();
        g_audio_arm7_boot[9]=2;
        int produced=produce_resident();g_audio_arm7_boot[9]=3;
        if(!produced){
            /* Wake on FIFO/timer/VBlank without the IntrWait software flag
             * handshake; service the mailbox on the next loop iteration. */
            g_audio_arm7_boot[9]=4;
#if DOK_ARM7_IDLE_BIOS
            swiHalt();
#else
            /* Compatibility candidate: IRQs return on the reported emulator
             * while the BIOS wait never resumes main. Keep every idle pass
             * bounded and stay outside SWI/HALT until that path is resolved. */
            volatile uint32_t delay=256;
            while(delay--)__asm__ volatile("nop" ::: "memory");
#endif
            g_audio_arm7_boot[9]=5;
        }
    }
    stop_voice();
    return 0;
}
