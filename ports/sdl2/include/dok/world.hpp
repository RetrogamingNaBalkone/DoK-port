#pragma once
#include "level.hpp"
#include "clock.hpp"
#include "events.hpp"
#include "audio.hpp"
#include <optional>

namespace dok {
struct Pose {std::int32_t x{},y{},z{};std::int16_t heading{};};
enum class GameEvent : CallbackId { player_tick = 1, door_open, door_close, door_auto, weapon_tick, weapon_switch, projectile_tick, monster_tick, base_animation };
struct AnimationState {
    std::int16_t frame=0,parameter_a=0,parameter_b=0,interval=0,remaining=0;
};
// Confirmed constructor words; semantic consumers are audited in SP07/SP10.
struct SwitchState {std::int16_t native_54=0,native_56=0,native_58=0,interval=0;};
struct SurfaceState {std::int16_t phase=18,interval=0;};
struct MonsterState {
    std::int16_t health=0,state=0,next_frame=-1,display_frame=0,display_clip=0;
    std::int16_t course_counter=0,shot_count=0;
    Stopwatch burst_clock;
    bool corpse=false;
};
struct ProjectileState {
    Pose pose;
    std::size_t definition_index=0;
    OwnerId source=0;
    std::int16_t damage=0,damage_floor=0,frame=0;
    std::int32_t speed=0;
    std::uint16_t flags=0;
    bool impact=false,actor_impact=false,effect_frame=false;
    std::optional<std::size_t> occupied_cell;
    Stopwatch clock;
};
struct WeaponState {std::int16_t state=4,shift=0,frame=0;bool acquired=false;};
struct DoorState {
    std::int16_t phase=0,extent=1,interval=0;
    std::uint8_t state=2;
    bool ready=true;
};
struct PlayerState {
    // LEVEL words +5e..+72. Unnamed intermediate fields are retained until
    // their gameplay consumers have been recovered, never discarded as padding.
    std::array<std::int16_t,11> attributes{};
    std::array<std::int16_t,3> extra_attributes{}; // Original +76/+78/+7a, categories 12..14.
    std::array<OwnerId,9> weapons{};
    std::int16_t current_weapon = 0, pending_weapon = 0;
    std::int16_t lives = 3, score = 0;
    std::uint32_t native_da = 0;
    std::int16_t health() const noexcept { return attributes[0]; }
    std::int16_t charge() const noexcept { return attributes[1]; }
    std::int16_t max_health() const noexcept { return attributes[2]; }
    std::int16_t max_charge() const noexcept { return attributes[3]; }
    std::uint16_t keys() const noexcept { return static_cast<std::uint16_t>(attributes[10]); }
};
struct MotionState {
    Stopwatch clock;
    std::int32_t acceleration=0,turn_acceleration=0;
    std::int16_t turn_direction=0;
    float last_forward=0,last_strafe=0;
    float mouse_remainder=0;
};
struct WorldObject {
    std::size_t definition_index{};
    std::optional<Pose> pose;
    unsigned placements{};
    OwnerId owner{};
    std::optional<PlayerState> player_state;
    std::uint16_t flags{};
    std::int16_t door_occupants=0;
    std::optional<MotionState> motion;
    std::optional<DoorState> door;
    std::optional<WeaponState> weapon;
    std::optional<MonsterState> monster;
    std::optional<AnimationState> animation;
    std::optional<SwitchState> switch_state;
    std::optional<SurfaceState> surface;
};
class World {
public:
    explicit World(LevelDefinition definition);
    // The caller must supply recovered gameplay callbacks. No silent no-op
    // handler: loading a world alone is not a running simulation.
    void advance_events(std::uint64_t nanoseconds, const Events::Invoke& handler);
    std::uint64_t simulation_ticks() const noexcept { return simulated_ticks_; }
    double presentation_ticks() const noexcept { return double(simulated_ticks_)+double(clock_.remainder())/GameClock::denominator; }
    void notify(const std::string& text);
    void notify(const ObjectDefinition& object,int offset);
    std::string visible_message() const;
    void sound(OwnerId owner,AudioAction action,unsigned binding=0x4c);
    std::function<void(const AudioCommand&)> audio;
    WorldObject& object_for_owner(OwnerId owner);
    const WorldObject& object_for_owner(OwnerId owner) const;
    LevelDefinition definition;
    std::map<std::string,std::vector<std::uint8_t>> maps;
    std::vector<std::uint8_t> visited;
    std::vector<WorldObject> objects;
    std::array<std::optional<ProjectileState>,256> projectiles;
    std::vector<std::int16_t> projectile_occupancy; // -1 or dynamic glyph slot.
    std::optional<std::size_t> player;
    std::int16_t total_money{};
    bool map_revealed=false,level_complete=false;
    std::string message; // Original CP866 bytes; retained for save-state support.
    std::uint64_t message_until=0;
    Events events;
private:
    GameClock clock_;
    std::uint64_t simulated_ticks_ = 0;
    bool advancing_ = false;
};
}
