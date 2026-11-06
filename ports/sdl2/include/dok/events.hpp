#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace dok {
using OwnerId = std::uint32_t;
using CallbackId = std::uint16_t;
struct Event {
    std::uint32_t deadline = 0, interval = 0;
    CallbackId callback = 0;
    std::uint32_t parameter = 0; // Native descriptor data, not a callback argument.
};

// One native dispatch(0) sweep. Logical IDs replace DOS pointers; callers own
// the clock and route callback IDs to recovered game operations.
class Events {
public:
    struct OwnerState {OwnerId id=0;std::uint16_t capacity=0;std::vector<Event> slots;};
    struct Snapshot {std::vector<OwnerState> owners;std::vector<OwnerId> ring;OwnerId cursor=0;};
    Snapshot snapshot() const;
    void restore(const Snapshot& state);
    using Clock = std::function<std::uint32_t()>;
    using Invoke = std::function<void(OwnerId, CallbackId)>;
    bool create(OwnerId id, std::uint16_t capacity = 4);
    bool destroy(OwnerId id);
    bool schedule(OwnerId id, CallbackId callback, std::int32_t interval,
                  std::uint32_t now, std::uint32_t parameter = 0);
    bool cancel_current() noexcept;
    bool set_first_interval(std::int32_t interval) noexcept;
    void clear();
    void dispatch(const Clock& clock, const Invoke& invoke);
    std::vector<Event> events(OwnerId id) const;
    std::size_t active_owners() const noexcept { return active_; }
private:
    struct Owner {
        std::vector<Event> slots;
        std::size_t count = 0;
        OwnerId next = 0, previous = 0;
        bool linked = false, pending = false, destroyed = false;
    };
    void link(OwnerId id);
    void unlink(OwnerId id);
    void finish();
    std::map<OwnerId, Owner> owners_;
    std::vector<OwnerId> pending_;
    OwnerId cursor_ = 0, current_ = 0;
    std::size_t active_ = 0;
    bool dispatching_ = false, callback_active_ = false, keep_ = true, clear_requested_ = false;
};
} // namespace dok
