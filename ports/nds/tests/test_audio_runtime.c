/* Real N04 package + production stream loader; deterministic fake consumer.
 * This verifies byte order, loop wrap, EOF padding and runtime ownership.
 * It does not execute ARM7, emulate timers, or establish acoustic quality. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform/audio/runtime.c"
static unsigned checks, clips_checked, odd_checked, compared, bounded_steps;
static unsigned reader_calls, resident_no_io;
#define CHECK(x) do { ++checks; if (!(x)) { \
 fprintf(stderr,"audio host line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static NdsAudioStatus consumer;
static int8_t *consumer_ring;
static uint32_t generation[2], source_length, source_cursor, consumer_loop, expected_hz;
static unsigned char expected[500000];
static _Alignas(32) unsigned char slab[262144];
static _Alignas(32) unsigned char extra_slab[163840];
static dok_asset_workspace workspace;
static dok_asset_session asset_session;
static int fail_reads;
static const unsigned char *mock_resident;
static uint32_t mock_resident_bytes;

NdsAudioStatus nds_audio_status(void) { return consumer; }
void nds_audio_set_loading(void (*progress)(unsigned)) { (void)progress; }
void nds_audio_service(void) {}
int nds_audio_init(void *data, size_t bytes)
{
    CHECK(bytes >= NDS_AUDIO_RING_BYTES);
    consumer_ring = data; memset(&consumer, 0, sizeof(consumer));
    mock_resident = NULL;
    consumer.ready = 1; return 1;
}
int nds_audio_write(unsigned part, const int8_t *pcm, size_t bytes)
{
    CHECK(part < 2 && bytes <= NDS_AUDIO_PART_BYTES);
    CHECK(!consumer.playing || !mock_resident);
    if (consumer.playing) {
        CHECK(consumer.consumed_parts &&
              part == ((consumer.consumed_parts - 1u) & 1u));
        generation[part] = consumer.consumed_parts + 1u;
    } else generation[part] = part;
    if (bytes) memcpy(consumer_ring + part * NDS_AUDIO_PART_BYTES, pcm, bytes);
    memset(consumer_ring + part * NDS_AUDIO_PART_BYTES + bytes, 0,
           NDS_AUDIO_PART_BYTES - bytes);
    return 1;
}
int nds_audio_begin(uint32_t hz, uint32_t length, int loop, uint32_t id)
{
    CHECK(!consumer.playing && length == source_length);
    CHECK(hz == expected_hz);
    CHECK(generation[0] == 0 && generation[1] == 1);
    mock_resident = NULL;
    consumer.playing = 1; consumer.identity = id;
    consumer.consumed_parts = consumer.consumed_samples = 0;
    consumer_loop = loop; source_cursor = 0; return 1;
}
int nds_audio_stop(uint32_t id)
{
    if (!id || id == consumer.identity) consumer.playing = 0;
    return !consumer.playing;
}
int nds_audio_volume(uint32_t volume) { return volume <= 127u; }
int nds_audio_resident(const unsigned char *packed, uint32_t bytes,
    uint32_t length, uint32_t hz, int loop, uint32_t id)
{
    CHECK(!consumer.playing && length == source_length && hz == expected_hz);
    CHECK(packed != NULL && bytes >= 8 && length <= 65536u);
    mock_resident = packed; mock_resident_bytes = bytes;
    consumer.playing = 1; consumer.identity = id;
    consumer.consumed_parts = consumer.consumed_samples = 0;
    consumer_loop = loop; source_cursor = 0;
    return 1;
}
static void consume_part(void)
{
    uint32_t ordinal = consumer.consumed_parts;
    uint32_t take = NDS_AUDIO_PART_BYTES;
    CHECK(consumer.playing);
    if (!consumer_loop && take > source_length - source_cursor)
        take = source_length - source_cursor;
    if (mock_resident) {
        uint32_t copied = 0, cursor = source_cursor;
        unsigned char *destination = (unsigned char *)consumer_ring +
                                     (ordinal & 1u) * NDS_AUDIO_PART_BYTES;
        while (copied < take) {
            uint32_t n = take - copied;
            if (n > source_length - cursor) n = source_length - cursor;
            CHECK(dok_audio_decode_range(mock_resident, mock_resident_bytes,
                source_length, cursor, destination + copied, n) == DOK_STATUS_OK);
            copied += n; cursor = (cursor + n) % source_length;
        }
        memset(destination + take, 0, NDS_AUDIO_PART_BYTES - take);
    } else CHECK(generation[ordinal & 1u] == ordinal);
    for (uint32_t i = 0; i < NDS_AUDIO_PART_BYTES; ++i) {
        unsigned char want = i < take ? expected[(source_cursor + i) % source_length] : 0;
        CHECK((unsigned char)consumer_ring[(ordinal & 1u) * NDS_AUDIO_PART_BYTES + i] == want);
        ++compared;
    }
    source_cursor += take;
    if (consumer_loop) source_cursor %= source_length;
    consumer.consumed_samples += take; ++consumer.consumed_parts;
    if (!consumer_loop && source_cursor == source_length) consumer.playing = 0;
    else if (!mock_resident && generation[consumer.consumed_parts & 1u] != consumer.consumed_parts) {
        ++consumer.underruns; consumer.playing = 0;
    }
}
static dok_asset_status read_at(void *context, uint32_t offset,
    unsigned char *destination, uint32_t requested, uint32_t *actual)
{
    FILE *file = context;
    ++reader_calls;
    if (fail_reads) return DOK_ASSET_IO_ERROR;
    if (fseek(file, (long)offset, SEEK_SET)) return DOK_ASSET_IO_ERROR;
    *actual = (uint32_t)fread(destination, 1, requested, file);
    return ferror(file) ? DOK_ASSET_IO_ERROR : DOK_ASSET_OK;
}
static uint32_t pcm_id(unsigned index)
{ return (SPRITE_LANGUAGE << 30) | (index << 18) | (DOK_ASSET_PCM_CLIP << 10); }
static void oracle(unsigned index)
{
    unsigned char descriptor[128]; dok_asset_view view;
    CHECK(dok_assets_load(&asset_session, pcm_id(index), descriptor,
                         sizeof(descriptor), &view) == DOK_ASSET_OK);
    expected_hz = le32(descriptor);
    source_length = le32(descriptor + 4); CHECK(source_length <= sizeof(expected));
    uint32_t copied = 0;
    for (uint32_t i = 0; i < le32(descriptor + 8); ++i) {
        CHECK(dok_assets_load(&asset_session, le32(descriptor + 24 + i * 12),
            expected + copied, (uint32_t)sizeof(expected) - copied, &view) == DOK_ASSET_OK);
        copied += view.entry.bytes;
    }
    CHECK(copied == source_length);
}
static int service_checked(void)
{
    uint32_t bytes_before = read_bytes, calls_before = read_calls;
    unsigned callback_before = reader_calls;
    int cold_title = pending_id && ((pending_pcm >> 18) & 4095u) == 110u;
    int result = nds_audio_runtime_service();
    if (!cold_title) {
        CHECK(read_bytes - bytes_before <= 512u);
        CHECK(read_calls - calls_before <= 1u);
        ++bounded_steps;
    }
    if (current) {
        CHECK(read_bytes == bytes_before && read_calls == calls_before);
        CHECK(reader_calls == callback_before);
    }
    return result;
}
static void refill_service(void)
{
    /* 66 bounded callbacks also cover the two split slices at an odd source
     * wrap. No fake time is consumed between these producer callbacks. */
    for (unsigned i = 0; i < 66; ++i) CHECK(service_checked());
    CHECK(!stream_filling);
}
static void clip(unsigned index, int loop)
{
    nds_audio_runtime_clear(); oracle(index);
    unsigned callback_before = reader_calls;
    uint32_t calls_before = read_calls;
    request(pcm_id(index), 0x80000000u | index, (uint32_t)loop, 0);
    CHECK(service_checked()); CHECK(consumer.playing);
    uint32_t id = consumer.identity, old_loop = looping;
    request(pcm_id(86), 1234, !loop, 0);
    CHECK(!pending_id && consumer.identity == id && looping == old_loop);
    stop(9999); CHECK(consumer.playing && consumer.identity == id);
    /* Service alone cannot consume time or terminate an audible voice. */
    CHECK(service_checked() && consumer.consumed_samples == 0);
    unsigned iterations = 0;
    do {
        consume_part(); refill_service(); ++iterations;
    } while (consumer.playing && (!loop || iterations < (index == 110 ? 20u : 9u)));
    if (!loop) CHECK(consumer.consumed_samples == source_length && !active_id);
    else { CHECK(consumer.playing); stop(id); CHECK(!consumer.playing && !active_id); }
    CHECK(!consumer.underruns && !fault);
    if (index != 110) {
        CHECK(reader_calls == callback_before && read_calls == calls_before);
        ++resident_no_io;
    }
    if (source_length & 3u) ++odd_checked;
    ++clips_checked;
}
static void epoch_guards(void)
{
    static dok_level_presentation dummy;
    const uint32_t current_epoch = 7;
    dok_object_ref object = {0}; object.object_id = 1;
    object.world_epoch = current_epoch;
    nds_audio_runtime_world(&dummy, current_epoch); oracle(86);
    request(pcm_id(86), identity(object, DOK_PRESENT_SOUND), 1, 0);
    CHECK(nds_audio_runtime_service());
    dok_interaction_output interaction = {0};
    interaction.object = object; interaction.object.world_epoch--;
    interaction.kind = DOK_INTERACTION_SOUND_STOP;
    nds_audio_runtime_interaction(&interaction);
    CHECK(consumer.playing && active_id);
    interaction.object.world_epoch = current_epoch;
    nds_audio_runtime_interaction(&interaction); CHECK(!consumer.playing && !active_id);

    request(pcm_id(86), identity(object, 0xf4), 0, 0);
    CHECK(nds_audio_runtime_service());
    dok_ai_output ai_output = {0}; ai_output.object = object;
    ai_output.object.world_epoch--; ai_output.kind = DOK_AI_DEAD;
    nds_audio_runtime_ai(NULL, &ai_output); CHECK(consumer.playing);
    ai_output.kind = DOK_AI_WAKE;
    nds_audio_runtime_ai(NULL, &ai_output); CHECK(consumer.playing && !pending_id);
    ai_output.object.world_epoch = current_epoch; ai_output.kind = DOK_AI_DEAD;
    nds_audio_runtime_ai(NULL, &ai_output); CHECK(!consumer.playing);

    request(pcm_id(86), identity(object, DOK_PRESENT_SOUND), 0, 0);
    CHECK(nds_audio_runtime_service());
    dok_combat_output combat_output = {0}; combat_output.object = object;
    combat_output.object.world_epoch--; combat_output.kind = DOK_COMBAT_REMOVED;
    nds_audio_runtime_combat(NULL, &combat_output); CHECK(consumer.playing);
    combat_output.object.world_epoch = current_epoch;
    nds_audio_runtime_combat(NULL, &combat_output); CHECK(!consumer.playing);
    request(pcm_id(86), identity(object, DOK_PRESENT_SOUND), 0, 0);
    CHECK(nds_audio_runtime_service() && consumer.playing);
    /* Native global stop precedes even a missing/invalid optional binding. */
    binding(object, 0, DOK_PRESENT_SOUND, 0, 1);
    CHECK(!consumer.playing && !pending_id && !active_id);
    nds_audio_runtime_clear(); CHECK(presentation == NULL);
}
int main(int argc, char **argv)
{
    CHECK(argc == 2); FILE *file = fopen(argv[1], "rb"); CHECK(file != NULL);
    CHECK(!fseek(file, 0, SEEK_END)); long file_bytes = ftell(file); CHECK(file_bytes > 0);
    dok_asset_reader reader = {file, read_at, (uint32_t)file_bytes};
    CHECK(dok_assets_open(&reader, &workspace, &asset_session) == DOK_ASSET_OK);
    CHECK(nds_audio_runtime_init(&asset_session, slab, sizeof(slab),
                                 extra_slab, sizeof(extra_slab)));
    CHECK(packed_used <= 327680u);
    for (unsigned i = 0; i < 22; ++i) {
        uintptr_t pointer = (uintptr_t)resident[i].packed;
        CHECK((pointer >= (uintptr_t)slab + 98304u &&
               pointer + resident[i].bytes <= (uintptr_t)slab + sizeof(slab)) ||
              (pointer >= (uintptr_t)extra_slab &&
               pointer + resident[i].bytes <= (uintptr_t)extra_slab + sizeof(extra_slab)));
    }
    for (unsigned i = 86; i <= 107; ++i) { clip(i, 0); clip(i, 1); }
    clip(110, 0); clip(110, 1);
    /* Pending single-voice rejection and forced replacement occur before I/O. */
    nds_audio_runtime_clear(); oracle(86);
    request(pcm_id(86), 86, 0, 0); request(pcm_id(87), 87, 1, 0);
    CHECK(pending_id == 86 && pending_loop == 0);
    request(pcm_id(86), 88, 1, 1); CHECK(pending_id == 88 && pending_loop == 1);
    CHECK(nds_audio_runtime_service() && consumer.identity == 88);
    nds_audio_runtime_clear(); CHECK(!consumer.playing && !pending_id && !active_id);
    epoch_guards();
    /* The original PCM is byte-identical across languages. A presentation
     * switch stops the previous voice but never repacks/frees ARM7 banks. */
    oracle(86);uint32_t old_reads=read_calls;
    request((pcm_id(86)&0x3fffffffu)|((3u-SPRITE_LANGUAGE)<<30),86,0,0);
    CHECK(nds_audio_runtime_service() && consumer.playing && read_calls==old_reads);
    while(consumer.playing){consume_part();CHECK(nds_audio_runtime_service());}
    CHECK(consumer.consumed_samples==source_length && !active_id);
    oracle(110); CHECK(nds_audio_runtime_title());
    CHECK(consumer.playing && !looping && consumer.identity == 0x8000006eu);
    while (consumer.playing) { consume_part(); refill_service(); }
    CHECK(consumer.consumed_samples == 474361u && !active_id);
    CHECK(nds_audio_runtime_service() && !consumer.playing);
    /* Resident effects remain usable if all later package reads fail. */
    oracle(86); fail_reads = 1; request(pcm_id(86), 86, 0, 0);
    CHECK(service_checked() && consumer.playing);
    while (consumer.playing) { consume_part(); refill_service(); }
    CHECK(!active_id && !fault);
    /* A failed title cold load cannot publish a new voice or old ring. */
    request(pcm_id(110), 110, 0, 0);
    CHECK(!nds_audio_runtime_service() && fault == 1 && !consumer.playing);
    nds_audio_runtime_clear(); CHECK(!pending_id && !active_id && !consumer.playing);
    fclose(file);
    printf("{\"passed\":true,\"language\":%u,\"clips\":%u,\"odd_length_cases\":%u,\"pcm_bytes_compared\":%u,\"resident_no_io_cases\":%u,\"bounded_service_steps\":%u,\"packed_bytes\":%u,\"checks\":%u,\"scope\":\"production loader with fake bounded consumer; no ARM7 execution\"}\n",
           (unsigned)SPRITE_LANGUAGE, clips_checked, odd_checked, compared,
           resident_no_io, bounded_steps, packed_used, checks);
    return 0;
}
