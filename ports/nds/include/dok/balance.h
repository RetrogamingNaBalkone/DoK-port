#ifndef DOK_NDS_BALANCE_H
#define DOK_NDS_BALANCE_H
#include "dok/world.h"

#define DOK_BALANCE_CHANGE_COUNT 39u
#define DOK_PRESENTATION_MAX_RESOURCES 146u
enum { DOK_BALANCE_ID=0x4E445335u, DOK_BALANCE_REVISION=1u };
typedef enum dok_balance_status {
    DOK_BALANCE_OK=0, DOK_BALANCE_INVALID_ARGUMENT=1,
    DOK_BALANCE_WRONG_SAMPLE=2, DOK_BALANCE_BAD_MODEL=3,
    DOK_BALANCE_BAD_PROFILE=4, DOK_BALANCE_RESOURCE_ERROR=5,
    DOK_BALANCE_NOT_FOUND=6
} dok_balance_status;
typedef enum dok_balance_reason {
    DOK_BALANCE_ENG_SPEED=1, DOK_BALANCE_ENG_WAKE=2,
    DOK_BALANCE_ENG_HEALTH=3, DOK_BALANCE_ENG_BOYAR_N=4,
    DOK_BALANCE_HYBRID_BABA_SPEED=5
} dok_balance_reason;
typedef struct dok_balance_change {
    uint16_t native_offset;
    uint8_t glyph, reason;
    int32_t before_ru, value;
} dok_balance_change;
typedef struct dok_balance_diff {
    uint32_t count, retained_assertions, boyar_definitions, mouse_definitions;
    dok_balance_change changes[DOK_BALANCE_CHANGE_COUNT];
} dok_balance_diff;

/* The immutable concrete implementation is private. This binds N03's opaque
 * descriptor to this exact accepted profile; arbitrary profiles are rejected. */
dok_balance_profile dok_balance_accepted_profile(void);
/* Actual CP866 bytes and deterministic fieldwise model digest are checked.
 * The archive identity is the SHA from an already validated N04 session;
 * this is a pinned-sample guard, not authentication of a hostile package.
 * base/candidate/out/diff are live, correctly typed and nonoverlapping.
 * Candidate may change on failure; out and diff remain unchanged. LEVEL2/3
 * publish a byte-for-byte model copy and an empty diff. Source stays borrowed. */
dok_balance_status dok_balance_apply(const dok_balance_profile *profile,
    const unsigned char archive_sha256[32], const dok_level_definition *base,
    dok_level_definition *candidate, dok_level_definition *out,
    dok_balance_diff *diff);
/* Fieldwise encoding defined in N05-balance.md; no padding or pointers hashed.
 * The full byte tokens, semantic values, identity and map planes are included. */
dok_balance_status dok_balance_model_digest(const dok_level_definition *level,
    unsigned char out[32]);

enum {
    DOK_PRESENT_SPRITE=0xFFF0u, DOK_PRESENT_ANIMATION=0xFFF1u,
    DOK_PRESENT_SOUND=0xFFF2u, DOK_PRESENT_PALETTE=0xFFF3u,
    DOK_PRESENT_LIGHT=0xFFF4u
};
enum {
    DOK_PRESENT_KIND_IMAGE=DOK_LEVEL_IMAGE,
    DOK_PRESENT_KIND_ANIMATION=DOK_LEVEL_ANIMATION,
    DOK_PRESENT_KIND_SOUND=DOK_LEVEL_SOUND,
    DOK_PRESENT_KIND_TEXT=DOK_LEVEL_TEXT,
    DOK_PRESENT_KIND_PALETTE=7u, DOK_PRESENT_KIND_LIGHT=8u
};
typedef struct dok_presentation_resource {
    dok_asset_id id;
    unsigned char name_cp866[16];
    uint8_t kind, reserved[3];
} dok_presentation_resource;
/* Immutable descriptor borrowing both parsed models, their pinned source
 * assets and the live N04 session. Numeric fields always come from effective
 * RU; this descriptor exposes only resource IDs and localized TEXT bytes. */
typedef struct dok_level_presentation {
    const dok_level_definition *effective, *localized;
    const dok_asset_session *session;
    uint32_t tag, resource_count;
    uint8_t language, reserved[3];
    dok_presentation_resource resources[DOK_PRESENTATION_MAX_RESOURCES];
} dok_level_presentation;
typedef struct dok_presentation_value {
    uint8_t kind, reserved[3];
    dok_asset_id asset_id;
    const unsigned char *text;
    uint32_t text_bytes;
} dok_presentation_value;
/* Build validates matching (glyph,class,field slot/kind), original localized
 * source bytes and every reference against exact CP866 name/language/kind.
 * candidate may change on failure, out remains unchanged. No allocation.
 * Keep both models, source leases and session live until descriptor expires. */
dok_balance_status dok_presentation_bind(const dok_level_definition *effective,
    const dok_level_definition *localized, const dok_asset_session *session,
    dok_level_presentation *candidate, dok_level_presentation *out);
/* Common slots above, or a native field offset for class IMAGE/ANIM/SOUND/TEXT.
 * Palette/light ignore glyph. Empty optional references return asset_id zero.
 * TEXT is borrowed CP866, capped at39bytes as in DOS; no numeric slot exists.
 * Failure preserves out. No model or world mutation occurs. */
dok_balance_status dok_presentation_get(const dok_level_presentation *presentation,
    uint8_t glyph, uint16_t slot, dok_presentation_value *out);
#endif
