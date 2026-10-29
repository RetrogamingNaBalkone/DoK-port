#include "dok/contracts.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return 1; \
    } \
} while (0)

static uint64_t read_counter(void *context)
{
    uint32_t *calls = context;
    (*calls)++;
    return UINT64_C(0x123456789ABCDEF0);
}

static int test_lifecycle(void)
{
    uint32_t calls = 0u;
    uint32_t catalog_payload = 7u;
    uint32_t balance_payload = 11u;
    dok_game_config config = {
        { &calls, read_counter, 33513982u, 64u, NULL },
        { 10u, 1u, &catalog_payload },
        { 20u, 2u, &balance_payload }
    };
    dok_game_state game = {0};
    dok_game_info info = {0};
    unsigned char before[sizeof(game)];
    unsigned char output_before[sizeof(info)];
    dok_game_config bad;

    CHECK(dok_game_initialize(&game, &config) == DOK_STATUS_OK);
    CHECK(dok_game_inspect(&game, &info) == DOK_STATUS_OK);
    CHECK(info.state == DOK_GAME_INITIALIZED);
    CHECK(info.lifecycle_generation == 1u);
    CHECK(info.asset_identity == 10u && info.asset_revision == 1u);
    CHECK(info.balance_identity == 20u && info.balance_revision == 2u);
    CHECK(info.world_epoch == 0u && info.has_world == 0u);
    CHECK(game._config.assets.implementation == &catalog_payload);
    CHECK(game._config.balance.implementation == &balance_payload);
    CHECK(game._config.platform.context == &calls);
    config.assets.identity = 99u;
    CHECK(game._config.assets.identity == 10u);

    memcpy(before, &game, sizeof(game));
    bad = config;
    bad.platform.counter_hz_denominator = 0u;
    CHECK(dok_game_initialize(&game, &bad) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(before, &game, sizeof(game)) == 0);
    bad = config;
    bad.platform.read_counter = NULL;
    CHECK(dok_game_initialize(&game, &bad) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(before, &game, sizeof(game)) == 0);
    bad = config;
    bad.balance.revision = 0u;
    CHECK(dok_game_initialize(&game, &bad) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(before, &game, sizeof(game)) == 0);
    CHECK(dok_game_initialize(&game, NULL) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(memcmp(before, &game, sizeof(game)) == 0);

    CHECK(dok_game_reset(&game) == DOK_STATUS_OK);
    CHECK(dok_game_inspect(&game, &info) == DOK_STATUS_OK);
    CHECK(info.lifecycle_generation == 2u && info.has_world == 0u);
    /* White-box boundary injection; application callers do not write fields. */
    game._lifecycle_generation = UINT32_MAX;
    memcpy(before, &game, sizeof(game));
    CHECK(dok_game_reset(&game) == DOK_STATUS_LIMIT_REACHED);
    CHECK(memcmp(before, &game, sizeof(game)) == 0);
    CHECK(dok_game_shutdown(&game) == DOK_STATUS_OK);
    CHECK(dok_game_shutdown(&game) == DOK_STATUS_OK);
    CHECK(dok_game_reset(&game) == DOK_STATUS_INVALID_STATE);
    CHECK(dok_game_inspect(&game, &info) == DOK_STATUS_OK);
    CHECK(info.state == DOK_GAME_SHUT_DOWN);
    CHECK(info.asset_identity == 0u && info.balance_identity == 0u);
    CHECK(game._config.assets.implementation == NULL);
    CHECK(game._config.balance.implementation == NULL);
    CHECK(game._config.platform.context == NULL);
    CHECK(catalog_payload == 7u && balance_payload == 11u);
    CHECK(calls == 0u);

    game._state = 0u;
    memset(&info, 0xA7, sizeof(info));
    memcpy(output_before, &info, sizeof(info));
    CHECK(dok_game_inspect(&game, &info) == DOK_STATUS_INVALID_STATE);
    CHECK(memcmp(output_before, &info, sizeof(info)) == 0);
    CHECK(dok_game_shutdown(&game) == DOK_STATUS_INVALID_STATE);
    config.assets.implementation = NULL;
    config.balance.implementation = NULL;
    CHECK(dok_game_initialize(&game, &config) == DOK_STATUS_OK);
    CHECK(dok_game_inspect(&game, &info) == DOK_STATUS_OK);
    CHECK(info.lifecycle_generation == 1u && info.has_world == 0u);
    CHECK(dok_game_shutdown(&game) == DOK_STATUS_OK);
    return 0;
}

static int test_input(void)
{
    dok_input_frame input = {0};
    CHECK(dok_input_frame_validate(&input) == DOK_STATUS_OK);
    input.held = DOK_BUTTON_MASK;
    input.pressed = DOK_BUTTON_MASK;
    input.sequence = UINT64_MAX;
    input.touch_flags = DOK_TOUCH_HELD | DOK_TOUCH_PRESSED;
    input.touch_x = 255;
    input.touch_y = 191;
    CHECK(dok_input_frame_validate(&input) == DOK_STATUS_OK);
    input.touch_x = 256;
    CHECK(dok_input_frame_validate(&input) == DOK_STATUS_INVALID_ARGUMENT);
    input.touch_x = 0;
    input.touch_y = -1;
    CHECK(dok_input_frame_validate(&input) == DOK_STATUS_INVALID_ARGUMENT);
    input.touch_y = 0;
    CHECK(dok_input_frame_validate(&input) == DOK_STATUS_OK);
    input.released = DOK_BUTTON_A;
    CHECK(dok_input_frame_validate(&input) == DOK_STATUS_INVALID_ARGUMENT);
    input.held = 0u;
    input.pressed = 0u;
    input.released = DOK_BUTTON_MASK;
    input.touch_flags = DOK_TOUCH_RELEASED;
    CHECK(dok_input_frame_validate(&input) == DOK_STATUS_OK);
    input.pressed = DOK_BUTTON_A;
    CHECK(dok_input_frame_validate(&input) == DOK_STATUS_INVALID_ARGUMENT);
    input.pressed = 0u;
    input.touch_flags = DOK_TOUCH_HELD | DOK_TOUCH_RELEASED;
    CHECK(dok_input_frame_validate(&input) == DOK_STATUS_INVALID_ARGUMENT);
    input.touch_flags = DOK_TOUCH_PRESSED;
    CHECK(dok_input_frame_validate(&input) == DOK_STATUS_INVALID_ARGUMENT);
    input.touch_flags = 8u;
    CHECK(dok_input_frame_validate(&input) == DOK_STATUS_INVALID_ARGUMENT);
    input.touch_flags = 0u;
    input.held = UINT32_C(0x1000);
    CHECK(dok_input_frame_validate(&input) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_input_frame_validate(NULL) == DOK_STATUS_INVALID_ARGUMENT);
    return 0;
}

static int test_identity(void)
{
    const dok_object_ref object = {1u, 2u, 3u};
    dok_object_ref other = object;
    CHECK(dok_object_ref_is_valid(object) == 1u);
    CHECK(dok_object_ref_equal(object, other) == 1u);
    other.generation++;
    CHECK(dok_object_ref_equal(object, other) == 0u);
    other = object;
    other.world_epoch++;
    CHECK(dok_object_ref_equal(object, other) == 0u);
    other = object;
    other.object_id++;
    CHECK(dok_object_ref_equal(object, other) == 0u);
    other = object;
    other.generation = 0u;
    CHECK(dok_object_ref_is_valid(other) == 0u);
    other = object;
    other.world_epoch = 0u;
    CHECK(dok_object_ref_is_valid(other) == 0u);
    other = object;
    other.object_id = 0u;
    CHECK(dok_object_ref_is_valid(other) == 0u);
    CHECK(dok_game_initialize(NULL, NULL) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_game_reset(NULL) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_game_shutdown(NULL) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_game_inspect(NULL, NULL) == DOK_STATUS_INVALID_ARGUMENT);
    return 0;
}

int main(void)
{
    if (test_lifecycle() || test_input() || test_identity()) {
        return 1;
    }
    puts("contracts: lifecycle, transactional refusal, identities and input passed");
    return 0;
}
