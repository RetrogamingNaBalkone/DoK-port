#include "input_cases.h"
#include "dok/controls.h"

#include <stddef.h>
#include <string.h>

typedef struct input_test_state {
    dok_input_test_report *report;
    dok_input_buffer *buffer;
} input_test_state;

#define CHECK(s, condition) do { \
    ++(s)->report->checks; \
    if (!(condition)) { \
        ++(s)->report->failures; \
        if ((s)->report->first_failed_line == 0u) \
            (s)->report->first_failed_line = (uint32_t)__LINE__; \
        return; \
    } \
} while (0)

static dok_input_frame frame(uint64_t sequence, uint32_t held,
                              uint32_t pressed, uint32_t released)
{
    dok_input_frame f = {0};
    f.sequence = sequence;
    f.held = held;
    f.pressed = pressed;
    f.released = released;
    return f;
}

static void mapping(input_test_state *s)
{
    /* Literal table independently encodes the approved physical-key layout.
     * Duplicate Use bits intentionally occur for B and X. */
    static const uint32_t expected[12] = {
        0x8u, 0x800u, 0u, 0u, 0x100000u, 0x80000u,
        0x10u, 0x20u, 0x200u, 0x2000u, 0x800u, 0x4u
    };
    uint32_t mask;
    for (mask = 0u; mask <= DOK_BUTTON_MASK; ++mask) {
        dok_input_commands command;
        dok_input_frame f = frame(1u, mask, mask, 0u);
        uint32_t wanted = 0u;
        uint32_t bit;
        for (bit = 0u; bit < 12u; ++bit)
            if ((mask & (UINT32_C(1) << bit)) != 0u) wanted |= expected[bit];
        CHECK(s, dok_input_map(&f, &command) == DOK_STATUS_OK);
        CHECK(s, command.native_held == wanted);
        CHECK(s, command.native_pressed == wanted && command.native_released == 0u);
        CHECK(s, command.next_weapon_pressed == ((mask & 4u) != 0u));
        CHECK(s, command.pause_pressed == ((mask & 8u) != 0u));
        f.held = 0u;
        f.pressed = 0u;
        f.released = mask;
        CHECK(s, dok_input_map(&f, &command) == DOK_STATUS_OK);
        CHECK(s, command.native_released == wanted && command.native_pressed == 0u &&
                 command.native_held == 0u);
        CHECK(s, command.pause_pressed == 0u && command.next_weapon_pressed == 0u);
        ++s->report->mapping_masks;
    }
    {
        dok_input_commands command;
        dok_input_frame f = frame(1u, 0u, 0u, 0u);
        uint32_t point;
        for (point = 0u; point < 4u; ++point) {
            f.touch_flags = DOK_TOUCH_HELD | DOK_TOUCH_PRESSED;
            f.touch_x = (int16_t)((point & 1u) != 0u ? 255 : 0);
            f.touch_y = (int16_t)((point & 2u) != 0u ? 191 : 0);
            CHECK(s, dok_input_map(&f, &command) == DOK_STATUS_OK);
            CHECK(s, command.native_held == 0u && command.native_pressed == 0u &&
                     command.native_released == 0u && command.pause_pressed == 0u &&
                     command.next_weapon_pressed == 0u);
        }
    }
}

static void ordered(input_test_state *s)
{
    uint64_t sequence = 0u;
    uint64_t counter = 100u;
    uint32_t round;
    CHECK(s, dok_input_buffer_initialize(s->buffer) == DOK_STATUS_OK);
    for (round = 0u; round < 16u; ++round) {
        uint32_t i;
        for (i = 0u; i < DOK_INPUT_SAMPLE_LIMIT; ++i) {
            uint32_t held = (i & 1u) == 0u ? DOK_BUTTON_R : 0u;
            dok_input_frame f = frame(++sequence, held, held,
                                       held == 0u ? DOK_BUTTON_R : 0u);
            CHECK(s, dok_input_capture(s->buffer, &f, counter) == DOK_STATUS_OK);
        }
        {
            dok_input_buffer before;
            dok_input_frame f = frame(sequence + 1u, DOK_BUTTON_A, DOK_BUTTON_A, 0u);
            memcpy(&before, s->buffer, sizeof(before));
            CHECK(s, dok_input_capture(s->buffer, &f, counter) == DOK_STATUS_NO_SPACE);
            CHECK(s, memcmp(&before, s->buffer, sizeof(before)) == 0);
        }
        for (i = 0u; i < DOK_INPUT_SAMPLE_LIMIT; ++i) {
            dok_input_sample out;
            dok_input_commands command;
            uint32_t available = 7u;
            uint32_t held = (i & 1u) == 0u ? DOK_BUTTON_R : 0u;
            CHECK(s, dok_input_pop_through(s->buffer, counter, &out, &available) ==
                     DOK_STATUS_OK);
            CHECK(s, available == 1u);
            CHECK(s, out.frame.sequence == sequence - DOK_INPUT_SAMPLE_LIMIT + i + 1u);
            CHECK(s, out.counter == counter && out.frame.held == held);
            CHECK(s, dok_input_map(&out.frame, &command) == DOK_STATUS_OK);
            CHECK(s, command.native_pressed == (held != 0u ? 0x200u : 0u));
            CHECK(s, command.native_released == (held == 0u ? 0x200u : 0u));
            ++s->report->ordered_samples;
        }
        ++counter;
    }
    {
        dok_input_sample out;
        dok_input_sample before;
        uint32_t available = 9u;
        dok_input_frame f = frame(++sequence, DOK_BUTTON_X, DOK_BUTTON_X, 0u);
        memset(&out, 0x5a, sizeof(out));
        memcpy(&before, &out, sizeof(out));
        CHECK(s, dok_input_capture(s->buffer, &f, counter + 1u) == DOK_STATUS_OK);
        CHECK(s, dok_input_pop_through(s->buffer, counter, &out, &available) == DOK_STATUS_OK);
        CHECK(s, available == 0u && memcmp(&out, &before, sizeof(out)) == 0);
        CHECK(s, dok_input_pop_through(s->buffer, counter + 1u, &out, &available) ==
                 DOK_STATUS_OK);
        CHECK(s, available == 1u && out.frame.pressed == DOK_BUTTON_X);
    }
}

static void suppression(input_test_state *s)
{
    uint32_t mask;
    for (mask = 0u; mask <= DOK_BUTTON_MASK; ++mask) {
        dok_input_frame f = frame(1u, mask, mask, 0u);
        dok_input_sample out;
        uint32_t available;
        CHECK(s, dok_input_buffer_initialize(s->buffer) == DOK_STATUS_OK);
        CHECK(s, dok_input_capture(s->buffer, &f, 10u) == DOK_STATUS_OK);
        CHECK(s, dok_input_ui_boundary(s->buffer) == DOK_STATUS_OK);
        CHECK(s, dok_input_pop_through(s->buffer, 10u, &out, &available) == DOK_STATUS_OK);
        CHECK(s, available == 0u);
        f = frame(2u, mask, 0u, 0u);
        CHECK(s, dok_input_capture(s->buffer, &f, 11u) == DOK_STATUS_OK);
        CHECK(s, dok_input_pop_through(s->buffer, 11u, &out, &available) == DOK_STATUS_OK);
        CHECK(s, available == 1u && out.frame.held == 0u && out.frame.pressed == 0u &&
                 out.frame.released == 0u);
        f = frame(3u, 0u, 0u, mask);
        CHECK(s, dok_input_capture(s->buffer, &f, 12u) == DOK_STATUS_OK);
        CHECK(s, dok_input_pop_through(s->buffer, 12u, &out, &available) == DOK_STATUS_OK);
        CHECK(s, available == 1u && out.frame.released == 0u);
        f = frame(4u, mask, mask, 0u);
        CHECK(s, dok_input_capture(s->buffer, &f, 13u) == DOK_STATUS_OK);
        CHECK(s, dok_input_pop_through(s->buffer, 13u, &out, &available) == DOK_STATUS_OK);
        CHECK(s, available == 1u && out.frame.held == mask && out.frame.pressed == mask);
        ++s->report->suppression_masks;
    }
    {
        dok_input_frame f = frame(1u, DOK_BUTTON_R, DOK_BUTTON_R, 0u);
        dok_input_sample out;
        uint32_t available;
        uint32_t i;
        CHECK(s, dok_input_buffer_initialize(s->buffer) == DOK_STATUS_OK);
        f.touch_flags = DOK_TOUCH_HELD | DOK_TOUCH_PRESSED;
        f.touch_x = 255;
        f.touch_y = 191;
        CHECK(s, dok_input_capture(s->buffer, &f, 1u) == DOK_STATUS_OK);
        CHECK(s, dok_input_ui_boundary(s->buffer) == DOK_STATUS_OK);
        for (i = 0u; i < 256u; ++i) {
            f.sequence++;
            f.pressed = 0u;
            f.touch_flags = DOK_TOUCH_HELD;
            CHECK(s, dok_input_capture(s->buffer, &f, 2u + i) == DOK_STATUS_OK);
            CHECK(s, dok_input_ui_boundary(s->buffer) == DOK_STATUS_OK);
        }
        f.sequence++;
        CHECK(s, dok_input_capture(s->buffer, &f, 258u) == DOK_STATUS_OK);
        CHECK(s, dok_input_pop_through(s->buffer, 258u, &out, &available) == DOK_STATUS_OK);
        CHECK(s, available == 1u && out.frame.touch_flags == 0u && out.frame.held == 0u);
        f.sequence++;
        f.held = 0u;
        f.released = DOK_BUTTON_R;
        f.touch_flags = DOK_TOUCH_RELEASED;
        CHECK(s, dok_input_capture(s->buffer, &f, 259u) == DOK_STATUS_OK);
        CHECK(s, dok_input_pop_through(s->buffer, 259u, &out, &available) == DOK_STATUS_OK);
        CHECK(s, available == 1u && out.frame.touch_flags == 0u && out.frame.released == 0u);
        f.sequence++;
        f.held = DOK_BUTTON_R;
        f.pressed = DOK_BUTTON_R;
        f.released = 0u;
        f.touch_flags = DOK_TOUCH_HELD | DOK_TOUCH_PRESSED;
        CHECK(s, dok_input_capture(s->buffer, &f, 260u) == DOK_STATUS_OK);
        CHECK(s, dok_input_pop_through(s->buffer, 260u, &out, &available) == DOK_STATUS_OK);
        CHECK(s, available == 1u && out.frame.pressed == DOK_BUTTON_R &&
                 out.frame.touch_flags == (DOK_TOUCH_HELD | DOK_TOUCH_PRESSED) &&
                 out.frame.touch_x == 255 && out.frame.touch_y == 191);
    }
    {
        /* Blocking one key must not swallow a different new gameplay key;
         * release removes only its own blocked bit. */
        dok_input_frame f = frame(1u, DOK_BUTTON_R | DOK_BUTTON_B,
                                   DOK_BUTTON_R | DOK_BUTTON_B, 0u);
        dok_input_sample out;
        uint32_t available;
        CHECK(s, dok_input_buffer_initialize(s->buffer) == DOK_STATUS_OK);
        CHECK(s, dok_input_capture(s->buffer, &f, 1u) == DOK_STATUS_OK);
        CHECK(s, dok_input_ui_boundary(s->buffer) == DOK_STATUS_OK);
        f = frame(2u, DOK_BUTTON_R | DOK_BUTTON_A, DOK_BUTTON_A, DOK_BUTTON_B);
        CHECK(s, dok_input_capture(s->buffer, &f, 2u) == DOK_STATUS_OK);
        CHECK(s, dok_input_pop_through(s->buffer, 2u, &out, &available) == DOK_STATUS_OK);
        CHECK(s, available == 1u && out.frame.held == DOK_BUTTON_A &&
                 out.frame.pressed == DOK_BUTTON_A && out.frame.released == 0u);
        f = frame(3u, DOK_BUTTON_R | DOK_BUTTON_B, DOK_BUTTON_B, DOK_BUTTON_A);
        CHECK(s, dok_input_capture(s->buffer, &f, 3u) == DOK_STATUS_OK);
        CHECK(s, dok_input_pop_through(s->buffer, 3u, &out, &available) == DOK_STATUS_OK);
        CHECK(s, available == 1u && out.frame.held == DOK_BUTTON_B &&
                 out.frame.pressed == DOK_BUTTON_B && out.frame.released == DOK_BUTTON_A);
    }
}

static void refusals(input_test_state *s)
{
    dok_input_buffer before;
    dok_input_frame f = frame(100u, DOK_BUTTON_A, DOK_BUTTON_A, 0u);
    dok_input_sample out;
    dok_input_commands command = {1u, 2u, 3u, 4u, 5u};
    dok_input_commands command_before = command;
    uint32_t available = 17u;
    CHECK(s, dok_input_buffer_initialize(s->buffer) == DOK_STATUS_OK);
    CHECK(s, dok_input_capture(s->buffer, &f, UINT64_MAX - 2u) == DOK_STATUS_OK);
    memcpy(&before, s->buffer, sizeof(before));
    CHECK(s, dok_input_capture(s->buffer, &f, UINT64_MAX - 2u) == DOK_STATUS_INVALID_STATE);
    CHECK(s, memcmp(&before, s->buffer, sizeof(before)) == 0);
    f.sequence++;
    CHECK(s, dok_input_capture(s->buffer, &f, UINT64_MAX - 3u) == DOK_STATUS_INVALID_STATE);
    CHECK(s, memcmp(&before, s->buffer, sizeof(before)) == 0);
    f.pressed |= DOK_BUTTON_B;
    CHECK(s, dok_input_capture(s->buffer, &f, UINT64_MAX) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(s, memcmp(&before, s->buffer, sizeof(before)) == 0);
    CHECK(s, dok_input_map(&f, &command) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(s, memcmp(&command, &command_before, sizeof(command)) == 0);
    f.pressed = DOK_BUTTON_A;
    f.sequence = UINT64_MAX;
    CHECK(s, dok_input_capture(s->buffer, &f, UINT64_MAX) == DOK_STATUS_OK);
    CHECK(s, dok_input_pop_through(s->buffer, UINT64_MAX, &out, &available) == DOK_STATUS_OK);
    memcpy(&before, s->buffer, sizeof(before));
    CHECK(s, dok_input_pop_through(s->buffer, UINT64_MAX - 1u, &out, &available) ==
             DOK_STATUS_INVALID_STATE);
    CHECK(s, memcmp(&before, s->buffer, sizeof(before)) == 0);
    CHECK(s, dok_input_capture(s->buffer, &f, UINT64_MAX) == DOK_STATUS_INVALID_STATE);
    CHECK(s, memcmp(&before, s->buffer, sizeof(before)) == 0);
    CHECK(s, dok_input_capture(NULL, &f, 0u) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(s, dok_input_capture(s->buffer, NULL, 0u) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(s, dok_input_map(NULL, &command) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(s, dok_input_map(&f, NULL) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(s, dok_input_ui_boundary(NULL) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(s, dok_input_pop_through(s->buffer, 0u, NULL, &available) ==
             DOK_STATUS_INVALID_ARGUMENT);
    CHECK(s, dok_input_pop_through(s->buffer, 0u, &out, NULL) ==
             DOK_STATUS_INVALID_ARGUMENT);
    CHECK(s, dok_input_buffer_initialize(NULL) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(s, dok_input_buffer_initialize(s->buffer) == DOK_STATUS_OK);
    CHECK(s, dok_input_pop_through(s->buffer, 100u, &out, &available) == DOK_STATUS_OK);
    f = frame(1u, 0u, 0u, 0u);
    memcpy(&before, s->buffer, sizeof(before));
    CHECK(s, dok_input_capture(s->buffer, &f, 99u) == DOK_STATUS_INVALID_STATE);
    CHECK(s, memcmp(&before, s->buffer, sizeof(before)) == 0);
    CHECK(s, dok_input_capture(s->buffer, &f, 100u) == DOK_STATUS_OK);
    CHECK(s, dok_input_pop_through(s->buffer, 100u, &out, &available) == DOK_STATUS_OK);
    CHECK(s, available == 1u && out.counter == 100u);
    s->buffer->_tag = 0u;
    CHECK(s, dok_input_ui_boundary(s->buffer) == DOK_STATUS_INVALID_STATE);
    CHECK(s, dok_input_capture(s->buffer, &f, 100u) == DOK_STATUS_INVALID_STATE);
    CHECK(s, dok_input_pop_through(s->buffer, 100u, &out, &available) == DOK_STATUS_INVALID_STATE);
}

static void integration(input_test_state *s)
{
    dok_control_latch latch;
    dok_control_commands commands;
    dok_input_sample sample;
    dok_input_frame f;
    uint32_t available;
    uint32_t i;
    uint32_t physical = DOK_BUTTON_UP | DOK_BUTTON_DOWN | DOK_BUTTON_LEFT |
                        DOK_BUTTON_RIGHT | DOK_BUTTON_Y | DOK_BUTTON_A |
                        DOK_BUTTON_R | DOK_BUTTON_L | DOK_BUTTON_SELECT |
                        DOK_BUTTON_START | DOK_BUTTON_X | DOK_BUTTON_B;
    uint32_t native = 0x4u | 0x8u | 0x10u | 0x20u | 0x200u | 0x800u |
                      0x2000u | 0x80000u | 0x100000u;
    CHECK(s, dok_input_buffer_initialize(s->buffer) == DOK_STATUS_OK);
    CHECK(s, dok_controls_initialize(&latch) == DOK_STATUS_OK);
    f = frame(1u, physical, physical, 0u);
    CHECK(s, dok_input_capture(s->buffer, &f, 10u) == DOK_STATUS_OK);
    f = frame(2u, 0u, 0u, physical);
    CHECK(s, dok_input_capture(s->buffer, &f, 11u) == DOK_STATUS_OK);
    for (i = 0u; i < 2u; ++i) {
        CHECK(s, dok_input_pop_through(s->buffer, 20u, &sample, &available) ==
                 DOK_STATUS_OK);
        CHECK(s, available == 1u);
        CHECK(s, dok_controls_feed(&latch, &sample.frame) == DOK_STATUS_OK);
    }
    CHECK(s, dok_controls_take(&latch, &commands) == DOK_STATUS_OK);
    CHECK(s, commands.physical_held == 0u && commands.physical_pressed == physical &&
             commands.physical_released == physical);
    CHECK(s, commands.native_commands == native && commands.use_triggered == 1u &&
             commands.next_weapon_pressed == 1u && commands.pause_pressed == 1u);
    CHECK(s, dok_controls_take(&latch, &commands) == DOK_STATUS_OK);
    CHECK(s, commands.native_commands == 0u && commands.use_triggered == 0u &&
             commands.next_weapon_pressed == 0u && commands.pause_pressed == 0u);
    ++s->report->integration_cases;

    /* X+B together is one Use; later individual samples remain separate
     * actions even though all four physical samples drain before service. */
    f = frame(3u, DOK_BUTTON_X | DOK_BUTTON_B, DOK_BUTTON_X | DOK_BUTTON_B, 0u);
    CHECK(s, dok_input_capture(s->buffer, &f, 21u) == DOK_STATUS_OK);
    f = frame(4u, 0u, 0u, DOK_BUTTON_X | DOK_BUTTON_B);
    CHECK(s, dok_input_capture(s->buffer, &f, 22u) == DOK_STATUS_OK);
    f = frame(5u, DOK_BUTTON_X, DOK_BUTTON_X, 0u);
    CHECK(s, dok_input_capture(s->buffer, &f, 23u) == DOK_STATUS_OK);
    f = frame(6u, DOK_BUTTON_B, DOK_BUTTON_B, DOK_BUTTON_X);
    CHECK(s, dok_input_capture(s->buffer, &f, 24u) == DOK_STATUS_OK);
    f = frame(7u, 0u, 0u, DOK_BUTTON_B);
    CHECK(s, dok_input_capture(s->buffer, &f, 25u) == DOK_STATUS_OK);
    for (i = 0u; i < 5u; ++i) {
        CHECK(s, dok_input_pop_through(s->buffer, 25u, &sample, &available) ==
                 DOK_STATUS_OK);
        CHECK(s, available == 1u);
        CHECK(s, dok_controls_feed(&latch, &sample.frame) == DOK_STATUS_OK);
    }
    for (i = 0u; i < 3u; ++i) {
        CHECK(s, dok_controls_take(&latch, &commands) == DOK_STATUS_OK);
        CHECK(s, commands.use_triggered == 1u && commands.native_commands == 0x800u);
    }
    CHECK(s, dok_controls_take(&latch, &commands) == DOK_STATUS_OK);
    CHECK(s, commands.use_triggered == 0u && commands.native_commands == 0u);
    ++s->report->integration_cases;

    /* Native Use consumes release, not make/held. Fire/movement/run are levels. */
    f = frame(8u, DOK_BUTTON_X | DOK_BUTTON_B | DOK_BUTTON_R | DOK_BUTTON_UP | DOK_BUTTON_L,
                  DOK_BUTTON_X | DOK_BUTTON_B | DOK_BUTTON_R | DOK_BUTTON_UP | DOK_BUTTON_L, 0u);
    CHECK(s, dok_input_capture(s->buffer, &f, 26u) == DOK_STATUS_OK);
    CHECK(s, dok_input_pop_through(s->buffer, 26u, &sample, &available) == DOK_STATUS_OK);
    CHECK(s, available == 1u);
    CHECK(s, dok_controls_feed(&latch, &sample.frame) == DOK_STATUS_OK);
    CHECK(s, dok_controls_take(&latch, &commands) == DOK_STATUS_OK);
    CHECK(s, commands.use_triggered == 0u && commands.native_commands == (0x200u | 0x10u | 0x2000u));
    for (i = 0u; i < 8u; ++i) {
        CHECK(s, dok_controls_take(&latch, &commands) == DOK_STATUS_OK);
        CHECK(s, commands.use_triggered == 0u && commands.native_commands == (0x200u | 0x10u | 0x2000u));
    }
    f = frame(9u, DOK_BUTTON_R | DOK_BUTTON_UP | DOK_BUTTON_L, 0u,
                  DOK_BUTTON_X | DOK_BUTTON_B);
    CHECK(s, dok_input_capture(s->buffer, &f, 27u) == DOK_STATUS_OK);
    CHECK(s, dok_input_pop_through(s->buffer, 27u, &sample, &available) == DOK_STATUS_OK);
    CHECK(s, available == 1u);
    CHECK(s, dok_controls_feed(&latch, &sample.frame) == DOK_STATUS_OK);
    CHECK(s, dok_controls_take(&latch, &commands) == DOK_STATUS_OK);
    CHECK(s, commands.use_triggered == 1u && commands.native_commands == (0x800u | 0x200u | 0x10u | 0x2000u));
    CHECK(s, dok_controls_take(&latch, &commands) == DOK_STATUS_OK);
    CHECK(s, commands.use_triggered == 0u && commands.native_commands == (0x200u | 0x10u | 0x2000u));
    ++s->report->integration_cases;

    /* Pending release Use before a UI transition must not become
     * gameplay later, while keys/touch held across return stay suppressed. */
    f = frame(10u, DOK_BUTTON_R | DOK_BUTTON_B,
                 DOK_BUTTON_B, DOK_BUTTON_UP | DOK_BUTTON_L);
    CHECK(s, dok_input_capture(s->buffer, &f, 30u) == DOK_STATUS_OK);
    CHECK(s, dok_input_pop_through(s->buffer, 30u, &sample, &available) == DOK_STATUS_OK);
    CHECK(s, available == 1u);
    CHECK(s, dok_controls_feed(&latch, &sample.frame) == DOK_STATUS_OK);
    f = frame(11u, DOK_BUTTON_R, 0u, DOK_BUTTON_B);
    CHECK(s, dok_input_capture(s->buffer, &f, 31u) == DOK_STATUS_OK);
    CHECK(s, dok_input_pop_through(s->buffer, 31u, &sample, &available) == DOK_STATUS_OK);
    CHECK(s, available == 1u);
    CHECK(s, dok_controls_feed(&latch, &sample.frame) == DOK_STATUS_OK);
    f = frame(12u, DOK_BUTTON_R | DOK_BUTTON_B, DOK_BUTTON_B, 0u);
    CHECK(s, dok_input_capture(s->buffer, &f, 32u) == DOK_STATUS_OK);
    CHECK(s, dok_input_ui_boundary(s->buffer) == DOK_STATUS_OK);
    CHECK(s, dok_controls_ui_boundary(&latch) == DOK_STATUS_OK);
    CHECK(s, dok_controls_take(&latch, &commands) == DOK_STATUS_OK);
    CHECK(s, commands.native_commands == 0u && commands.physical_pressed == 0u &&
             commands.physical_held == 0u && commands.use_triggered == 0u);
    f = frame(13u, DOK_BUTTON_R | DOK_BUTTON_B, 0u, 0u);
    CHECK(s, dok_input_capture(s->buffer, &f, 33u) == DOK_STATUS_OK);
    CHECK(s, dok_input_pop_through(s->buffer, 33u, &sample, &available) == DOK_STATUS_OK);
    CHECK(s, available == 1u);
    CHECK(s, dok_controls_feed(&latch, &sample.frame) == DOK_STATUS_OK);
    CHECK(s, dok_controls_take(&latch, &commands) == DOK_STATUS_OK);
    CHECK(s, commands.native_commands == 0u && commands.physical_released == 0u);
    f = frame(14u, 0u, 0u, DOK_BUTTON_R | DOK_BUTTON_B);
    CHECK(s, dok_input_capture(s->buffer, &f, 34u) == DOK_STATUS_OK);
    CHECK(s, dok_input_pop_through(s->buffer, 34u, &sample, &available) == DOK_STATUS_OK);
    CHECK(s, available == 1u && sample.frame.released == 0u);
    CHECK(s, dok_controls_feed(&latch, &sample.frame) == DOK_STATUS_OK);
    f = frame(15u, DOK_BUTTON_R | DOK_BUTTON_B,
                 DOK_BUTTON_R | DOK_BUTTON_B, 0u);
    CHECK(s, dok_input_capture(s->buffer, &f, 35u) == DOK_STATUS_OK);
    CHECK(s, dok_input_pop_through(s->buffer, 35u, &sample, &available) == DOK_STATUS_OK);
    CHECK(s, available == 1u);
    CHECK(s, dok_controls_feed(&latch, &sample.frame) == DOK_STATUS_OK);
    CHECK(s, dok_controls_take(&latch, &commands) == DOK_STATUS_OK);
    CHECK(s, commands.native_commands == 0x200u && commands.use_triggered == 0u);
    f = frame(16u, 0u, 0u, DOK_BUTTON_R | DOK_BUTTON_B);
    CHECK(s, dok_input_capture(s->buffer, &f, 36u) == DOK_STATUS_OK);
    CHECK(s, dok_input_pop_through(s->buffer, 36u, &sample, &available) == DOK_STATUS_OK);
    CHECK(s, available == 1u);
    CHECK(s, dok_controls_feed(&latch, &sample.frame) == DOK_STATUS_OK);
    CHECK(s, dok_controls_take(&latch, &commands) == DOK_STATUS_OK);
    CHECK(s, commands.native_commands == 0x800u && commands.use_triggered == 1u);
    ++s->report->integration_cases;

    {
        /* White-box saturation is a limit test, not a public state edit.
         * Every pending field must survive any preflight overflow refusal. */
        dok_control_latch before;
        CHECK(s, dok_controls_initialize(&latch) == DOK_STATUS_OK);
        latch._presses[8] = UINT32_MAX;
        memcpy(&before, &latch, sizeof(before));
        f = frame(1u, DOK_BUTTON_R | DOK_BUTTON_X,
                     DOK_BUTTON_R | DOK_BUTTON_X, 0u);
        CHECK(s, dok_controls_feed(&latch, &f) == DOK_STATUS_LIMIT_REACHED);
        CHECK(s, memcmp(&before, &latch, sizeof(before)) == 0);
        CHECK(s, dok_controls_initialize(&latch) == DOK_STATUS_OK);
        latch._releases[0] = UINT32_MAX;
        memcpy(&before, &latch, sizeof(before));
        f = frame(1u, DOK_BUTTON_R, DOK_BUTTON_R, DOK_BUTTON_A);
        CHECK(s, dok_controls_feed(&latch, &f) == DOK_STATUS_LIMIT_REACHED);
        CHECK(s, memcmp(&before, &latch, sizeof(before)) == 0);
        CHECK(s, dok_controls_initialize(&latch) == DOK_STATUS_OK);
        latch._uses = UINT32_MAX;
        memcpy(&before, &latch, sizeof(before));
        f = frame(1u, 0u, 0u, DOK_BUTTON_X);
        CHECK(s, dok_controls_feed(&latch, &f) == DOK_STATUS_LIMIT_REACHED);
        CHECK(s, memcmp(&before, &latch, sizeof(before)) == 0);
        CHECK(s, dok_controls_initialize(&latch) == DOK_STATUS_OK);
        CHECK(s, dok_controls_feed(&latch, &f) == DOK_STATUS_OK);
        memcpy(&before, &latch, sizeof(before));
        CHECK(s, dok_controls_feed(&latch, &f) == DOK_STATUS_INVALID_STATE);
        CHECK(s, memcmp(&before, &latch, sizeof(before)) == 0);
        ++s->report->integration_cases;
    }
}

int dok_input_run_cases(dok_input_test_report *report, dok_input_buffer *buffer)
{
    input_test_state state;
    if (report == NULL || buffer == NULL) return 1;
    memset(report, 0, sizeof(*report));
    state.report = report;
    state.buffer = buffer;
    mapping(&state);
    ordered(&state);
    suppression(&state);
    refusals(&state);
    integration(&state);
    return report->failures != 0u;
}
