#include "dok/contracts.h"

#include <stddef.h>

_Static_assert(sizeof(uint16_t) == 2u, "16-bit values required");
_Static_assert(sizeof(uint32_t) == 4u, "32-bit values required");
_Static_assert(sizeof(uint64_t) == 8u, "64-bit counter required");

uint32_t dok_object_ref_is_valid(dok_object_ref reference)
{
    return reference.world_epoch != 0u && reference.object_id != 0u &&
           reference.generation != 0u;
}

uint32_t dok_object_ref_equal(dok_object_ref left, dok_object_ref right)
{
    return left.world_epoch == right.world_epoch &&
           left.object_id == right.object_id &&
           left.generation == right.generation;
}

dok_status dok_input_frame_validate(const dok_input_frame *input)
{
    if (input == NULL ||
        ((input->held | input->pressed | input->released) &
         ~(uint32_t)DOK_BUTTON_MASK) != 0u ||
        (input->pressed & ~input->held) != 0u ||
        (input->released & input->held) != 0u ||
        (input->touch_flags & ~UINT32_C(7)) != 0u) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (((input->touch_flags & DOK_TOUCH_PRESSED) != 0u &&
         (input->touch_flags & DOK_TOUCH_HELD) == 0u) ||
        ((input->touch_flags & DOK_TOUCH_RELEASED) != 0u &&
         (input->touch_flags & (DOK_TOUCH_HELD | DOK_TOUCH_PRESSED)) != 0u)) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if ((input->touch_flags & DOK_TOUCH_HELD) != 0u &&
        (input->touch_x < 0 || input->touch_x >= 256 ||
         input->touch_y < 0 || input->touch_y >= 192)) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    return DOK_STATUS_OK;
}

dok_status dok_game_config_validate(const dok_game_config *config)
{
    if (config == NULL || config->platform.read_counter == NULL ||
        config->platform.counter_hz_numerator == 0u ||
        config->platform.counter_hz_denominator == 0u ||
        config->assets.identity == 0u || config->assets.revision == 0u ||
        config->balance.identity == 0u || config->balance.revision == 0u) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    return DOK_STATUS_OK;
}

dok_status dok_game_initialize(dok_game_state *game,
                               const dok_game_config *config)
{
    dok_game_state initialized;
    if (game == NULL || dok_game_config_validate(config) != DOK_STATUS_OK) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    initialized._config = *config;
    initialized._lifecycle_generation = 1u;
    initialized._state = DOK_GAME_INITIALIZED;
    *game = initialized;
    return DOK_STATUS_OK;
}

dok_status dok_game_reset(dok_game_state *game)
{
    if (game == NULL) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (game->_state != DOK_GAME_INITIALIZED) {
        return DOK_STATUS_INVALID_STATE;
    }
    if (game->_lifecycle_generation == UINT32_MAX) {
        return DOK_STATUS_LIMIT_REACHED;
    }
    game->_lifecycle_generation++;
    return DOK_STATUS_OK;
}

dok_status dok_game_shutdown(dok_game_state *game)
{
    if (game == NULL) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (game->_state == DOK_GAME_SHUT_DOWN) {
        return DOK_STATUS_OK;
    }
    if (game->_state != DOK_GAME_INITIALIZED) {
        return DOK_STATUS_INVALID_STATE;
    }
    game->_config = (dok_game_config){0};
    game->_state = DOK_GAME_SHUT_DOWN;
    return DOK_STATUS_OK;
}

dok_status dok_game_inspect(const dok_game_state *game, dok_game_info *out)
{
    dok_game_info result;
    if (game == NULL || out == NULL) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (game->_state != DOK_GAME_INITIALIZED &&
        game->_state != DOK_GAME_SHUT_DOWN) {
        return DOK_STATUS_INVALID_STATE;
    }
    result.state = game->_state;
    result.lifecycle_generation = game->_lifecycle_generation;
    result.asset_identity = game->_config.assets.identity;
    result.asset_revision = game->_config.assets.revision;
    result.balance_identity = game->_config.balance.identity;
    result.balance_revision = game->_config.balance.revision;
    result.world_epoch = 0u;
    result.has_world = 0u;
    *out = result;
    return DOK_STATUS_OK;
}
