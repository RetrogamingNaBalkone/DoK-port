#ifndef DOK_NDS_ASSETS_H
#define DOK_NDS_ASSETS_H
#include "dok/contracts.h"
#include "dok/memory_budget.h"
#include <stdint.h>

#define DOK_ASSET_HEADER_BYTES 160u
#define DOK_ASSET_ENTRY_BYTES 128u
#define DOK_ASSET_MAX_ENTRIES 2048u
#define DOK_ASSET_READ_MAX 65536u
#define DOK_ASSET_IO_SCRATCH 32768u

typedef enum dok_asset_status {
    DOK_ASSET_OK=0, DOK_ASSET_INVALID_ARGUMENT=1,
    DOK_ASSET_IO_ERROR=2, DOK_ASSET_SHORT_READ=3,
    DOK_ASSET_TRUNCATED=4, DOK_ASSET_UNSUPPORTED_VERSION=5,
    DOK_ASSET_BAD_HEADER=6, DOK_ASSET_BAD_CATALOG=7,
    DOK_ASSET_BOUNDS=8, DOK_ASSET_HASH_MISMATCH=9,
    DOK_ASSET_MALFORMED_PAYLOAD=10, DOK_ASSET_NOT_FOUND=11,
    DOK_ASSET_NO_SPACE=12, DOK_ASSET_PINNED=13,
    DOK_ASSET_STALE_LEASE=14, DOK_ASSET_LIMIT_REACHED=15,
    DOK_ASSET_INVALID_STATE=16, DOK_ASSET_VERIFY_ONLY=17
} dok_asset_status;

typedef enum dok_asset_representation {
    DOK_ASSET_RAW_SOURCE=1, DOK_ASSET_IMAGE_INDEX8=2,
    DOK_ASSET_ANIM_META=3, DOK_ASSET_FRAME_INDEX8=4,
    DOK_ASSET_GPU_INDEX8_POT=5, DOK_ASSET_STATIC_INDEX8=6,
    DOK_ASSET_PALETTE_RGB555=7, DOK_ASSET_PCM_CLIP=8,
    DOK_ASSET_PCM_CHUNK=9, DOK_ASSET_TEXT_UTF8=10,
    DOK_ASSET_FONT_BITMAP=11, DOK_ASSET_LIGHT_TABLE=12,
    DOK_ASSET_LEVEL_MAPS=13, DOK_ASSET_LEVEL_CP866=14,
    DOK_ASSET_STATIC_GPU_INDEX8=15, DOK_ASSET_TEXT_CP866=16
} dok_asset_representation;

/* Read-only random access. On OK, actual <= requested; a short result is an
 * error, never retried as if it were complete. Callback may not mutate the
 * package. Reader/context remain live and immutable for the session lifetime.
 * Callback errors are propagated as IO_ERROR. No write callback exists. */
typedef dok_asset_status (*dok_asset_read_at_fn)(void *context, uint32_t offset,
    unsigned char *destination, uint32_t requested, uint32_t *actual);
typedef struct dok_asset_reader {
    void *context;
    dok_asset_read_at_fn read_at;
    uint32_t file_bytes;
} dok_asset_reader;

/* Native values, never a disk layout. No serialized pointers are used. */
typedef struct dok_asset_entry {
    dok_asset_id id, source_id;
    uint32_t offset, bytes;
    uint16_t representation;
    uint8_t language, source_kind;
    uint16_t source_index, subindex;
    uint16_t width, height, storage_width, storage_height;
    uint32_t param0, param1;
    unsigned char sha256[32], source_sha256[32], name_cp866[16];
} dok_asset_entry;

typedef struct dok_asset_session {
    dok_asset_reader _reader;
    uint32_t _tag, _entry_count, _data_offset;
    unsigned char _directory_sha256[32], _source_sha256[2][32];
} dok_asset_session;

typedef struct dok_asset_range {
    uint32_t offset, bytes;
    unsigned char sha256[32];
} dok_asset_range;
typedef struct dok_asset_workspace {
    dok_asset_range ranges[DOK_ASSET_MAX_ENTRIES];
    unsigned char io[DOK_ASSET_IO_SCRATCH];
} dok_asset_workspace;
_Static_assert(sizeof(dok_asset_workspace)<=DOK_NDS_IO_BYTES,
               "N04 catalog workspace fits N03 I/O ceiling");

/* Open validates header/catalog, source provenance consistency, exact aliases,
 * canonical range order and zero padding. It does not pre-load all payloads.
 * Caller provides separate typed workspace and output; failure preserves out.
 * Replacing a session while any borrower/lease depends on it is forbidden. */
dok_asset_status dok_assets_open(const dok_asset_reader *reader,
    dok_asset_workspace *workspace, dok_asset_session *out);
dok_asset_status dok_assets_catalog(const dok_asset_session *session,
    dok_asset_catalog *out);
dok_asset_status dok_assets_entry_at(const dok_asset_session *session,
    uint32_t index, dok_asset_entry *out);
dok_asset_status dok_assets_find(const dok_asset_session *session,
    dok_asset_id id, dok_asset_entry *out);
uint32_t dok_assets_count(const dok_asset_session *session);

typedef struct dok_asset_view {
    dok_asset_entry entry;
    const unsigned char *data;
} dok_asset_view;

/* Hash-only streaming verification is explicitly distinct from typed loading.
 * Useful for RAW_SOURCE, which is never exposed as a native object.
 * Workspace is transient; all output arguments remain unchanged on failure. */
dok_asset_status dok_assets_verify_hash(const dok_asset_session *session,
    dok_asset_id id, dok_asset_workspace *workspace);
/* Candidate is caller-owned, unleased, non-overlapping with session/out.
 * Bytes MAY change on failure. Only success publishes out after SHA and schema
 * validation, including descriptor links. RAW_SOURCE returns VERIFY_ONLY.
 * Candidate and all resident cache backing together fit the 1MiB asset pool;
 * this API neither allocates extra memory nor adds to that budget. */
dok_asset_status dok_assets_load(const dok_asset_session *session,
    dok_asset_id id, unsigned char *candidate, uint32_t capacity,
    dok_asset_view *out);

typedef struct dok_asset_cache_slot {
    unsigned char *_backing;
    uint32_t _capacity, _generation, _pins, _valid;
    dok_asset_entry _entry;
} dok_asset_cache_slot;
typedef struct dok_asset_lease {
    dok_asset_cache_slot *slot;
    uint32_t generation;
    dok_asset_view view;
} dok_asset_lease;

/* Caller partitions the pool; no fixed slot sizes, LRU or hidden allocator.
 * Init only unused storage. Backing must remain live through all leases.
 * Replace requires separate candidate; existing backing and metadata survive
 * failure. Success copies verified bytes then publishes one pinned lease.
 * Lease objects are linear: do not copy or release twice. Release clears the
 * supplied lease; generation/pin exhaustion fails without wrapping. */
dok_asset_status dok_asset_cache_initialize(dok_asset_cache_slot *slot,
    unsigned char *backing, uint32_t capacity);
dok_asset_status dok_asset_cache_replace(const dok_asset_session *session,
    dok_asset_id id, dok_asset_cache_slot *slot, unsigned char *candidate,
    uint32_t candidate_capacity, dok_asset_lease *out);
dok_asset_status dok_asset_cache_acquire(dok_asset_cache_slot *slot,
    dok_asset_lease *out);
dok_asset_status dok_asset_cache_release(dok_asset_lease *lease);
#endif
