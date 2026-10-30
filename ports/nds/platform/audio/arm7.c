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
static volatile NdsAudioShared *shared;
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
    SCHANNEL_LENGTH(0) = NDS_AUDIO_RING_BYTES / 4u;
    SCHANNEL_TIMER(0) = (uint16_t)(0u - period);
    TIMER0_CR = 0;
    TIMER0_DATA = (uint16_t)(0u - 2u * period);
    REG_IF = IRQ_TIMER1;
    schedule();
    shared->status.preparing = 0;
    shared->status.consumer_started = 1;
    SCHANNEL_CR(0) = SCHANNEL_ENABLE | SOUND_REPEAT |
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
static void receive(int bytes, void *unused)
{
    NdsAudioCommand message;
    (void)unused;
    if (bytes != sizeof(message)) {
        /* Drain malformed messages, never interpret a partial command. */
        uint8_t discard[128];
        fifoGetDatamsg(FIFO_USER_01, sizeof(discard), discard);
        if (shared) ++shared->status.stale_commands;
        return;
    }
    fifoGetDatamsg(FIFO_USER_01, sizeof(message), (u8 *)&message);
    if (message.magic != NDS_AUDIO_IPC_MAGIC) return;
    if (message.op == NDS_AUDIO_ATTACH) {
        stop_voice();
        /* ARM9 main RAM only; reject FIFO pointer values outside DS RAM. */
        if (message.shared < 0x02000000u ||
            message.shared > 0x02400000u - sizeof(NdsAudioShared) ||
            message.ring < 0x02000000u ||
            message.ring > 0x02400000u - NDS_AUDIO_RING_BYTES ||
            (message.shared & 31u) || (message.ring & 31u)) return;
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
    if (shared->status.preparing) start_channel();
    ++shared->status.status_sequence;
    leaveCriticalSection(saved);
    return 1;
}
static void power_callback(void) { exit_loop = true; }
static void vblank(void) { inputGetAndSend(); }
int main(void)
{
    enableSound(); readUserSettings(); ledBlink(LED_ALWAYS_ON); touchInit();
    irqInit(); fifoInit(); installSystemFIFO();
    if (isDSiMode()) installCameraFIFO();
    fifoSetDatamsgHandler(FIFO_USER_01, receive, 0);
    irqSet(IRQ_TIMER1, audio_irq); irqEnable(IRQ_TIMER1);
    setPowerButtonCB(power_callback);
    initClockIRQTimer(LIBNDS_DEFAULT_TIMER_RTC);
    irqSet(IRQ_VBLANK, vblank); irqEnable(IRQ_VBLANK);
    while (!exit_loop) {
        if (!produce_resident()) swiWaitForVBlank();
    }
    stop_voice();
    return 0;
}
