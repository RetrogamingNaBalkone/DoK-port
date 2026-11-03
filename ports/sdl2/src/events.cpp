#include "dok/events.hpp"
#include <stdexcept>

namespace dok {
bool Events::create(OwnerId id, std::uint16_t capacity) {
    if (!id || capacity < 1 || capacity > 32767 || owners_.count(id)) return false;
    Owner owner;
    owner.slots.resize(capacity - 1u);
    owners_.emplace(id, std::move(owner));
    return true;
}

void Events::link(OwnerId id) {
    auto& owner = owners_.at(id);
    if (!active_) owner.next = owner.previous = id;
    else {
        auto& cursor = owners_.at(cursor_);
        owner.next = cursor.next;
        owner.previous = cursor_;
        owners_.at(cursor.next).previous = id;
        cursor.next = id;
    }
    owner.linked = true;
    cursor_ = id;
    ++active_;
}

void Events::unlink(OwnerId id) {
    auto& owner = owners_.at(id);
    if (!owner.linked) return;
    owners_.at(owner.previous).next = owner.next;
    owners_.at(owner.next).previous = owner.previous;
    --active_;
    if (cursor_ == id) cursor_ = active_ ? owner.next : 0;
    owner.linked = false;
}

bool Events::destroy(OwnerId id) {
    auto found = owners_.find(id);
    if (found == owners_.end() || found->second.destroyed) return false;
    if (dispatching_) found->second.destroyed = true;
    else { unlink(id); owners_.erase(found); }
    return true;
}

bool Events::schedule(OwnerId id, CallbackId callback, std::int32_t interval,
                      std::uint32_t now, std::uint32_t parameter) {
    auto found = owners_.find(id);
    if (found == owners_.end() || !callback || found->second.destroyed || clear_requested_) return false;
    auto& owner = found->second;
    if (owner.count == owner.slots.size()) return false;
    if (!owner.count && !owner.linked && !owner.pending) {
        if (dispatching_) { owner.pending = true; pending_.push_back(id); }
        else link(id);
    }
    const auto bits = static_cast<std::uint32_t>(interval);
    owner.slots[owner.count++] = {now + bits, bits, callback, parameter};
    return true;
}

bool Events::cancel_current() noexcept {
    if (!callback_active_) return false;
    keep_ = false;
    return true;
}

bool Events::set_first_interval(std::int32_t interval) noexcept {
    if (!callback_active_) return false;
    owners_.at(current_).slots[0].interval = static_cast<std::uint32_t>(interval);
    return true;
}

void Events::clear() {
    if (dispatching_) { clear_requested_ = true; return; }
    for (auto& entry : owners_) {
        auto& owner = entry.second;
        owner.count = 0;
        owner.linked = owner.pending = false;
        owner.next = owner.previous = 0;
    }
    pending_.clear();
    active_ = 0;
    cursor_ = 0;
}

void Events::finish() {
    current_ = 0;
    callback_active_ = dispatching_ = false;
    for (auto it = owners_.begin(); it != owners_.end();) {
        if (it->second.destroyed) { unlink(it->first); it = owners_.erase(it); }
        else ++it;
    }
    if (clear_requested_) {
        clear_requested_ = false;
        clear();
    } else {
        for (auto id : pending_) {
            auto it = owners_.find(id);
            if (it == owners_.end()) continue;
            it->second.pending = false;
            if (it->second.count) link(id);
        }
        pending_.clear();
    }
}

void Events::dispatch(const Clock& clock, const Invoke& invoke) {
    if (dispatching_) throw std::logic_error("Recursive event dispatch");
    dispatching_ = true;
    try {
        // The shrinking bound is intentional: removing an empty owner can
        // postpone a neighbour until the next sweep, as in all three originals.
        for (std::size_t visited = 0; visited < active_ && !clear_requested_; ++visited) {
            current_ = cursor_;
            auto& owner = owners_.at(current_);
            std::size_t write = 0;
            for (std::size_t read = 0; read < owner.count && !owner.destroyed && !clear_requested_; ++read) {
                owner.slots[write] = owner.slots[read];
                keep_ = true;
                if (clock() >= owner.slots[read].deadline) {
                    callback_active_ = true;
                    invoke(current_, owner.slots[read].callback);
                    callback_active_ = false;
                    // Do not move this update to slots[write]: native compaction
                    // leaves the copied survivor's old deadline in that slot.
                    owner.slots[read].deadline = clock() + owner.slots[read].interval;
                }
                if (keep_) ++write;
            }
            owner.count = owner.destroyed ? 0 : write;
            const auto next = owner.next;
            if (!owner.count) unlink(current_);
            cursor_ = active_ ? next : 0;
        }
    } catch (...) { finish(); throw; }
    finish();
}

std::vector<Event> Events::events(OwnerId id) const {
    const auto found = owners_.find(id);
    if (found == owners_.end() || found->second.destroyed) return {};
    const auto& owner = found->second;
    return {owner.slots.begin(), owner.slots.begin() + owner.count};
}
} // namespace dok
