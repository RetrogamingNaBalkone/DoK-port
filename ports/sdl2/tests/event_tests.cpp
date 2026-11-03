#include "dok/events.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

static void check(bool value) { if (!value) throw std::runtime_error("Event vector failed"); }
int main() {
    try {
        std::uint32_t now = 0;
        const auto clock = [&] { return now; };
        dok::Events queue;
        std::vector<dok::OwnerId> order;
        for (auto id : {1u, 2u, 3u}) {
            check(queue.create(id));
            check(queue.schedule(id, 1, 10, now));
        }
        auto record = [&](dok::OwnerId id, dok::CallbackId) { order.push_back(id); };
        now = 9;
        queue.dispatch(clock, record);
        check(order.empty());
        now = 10;
        queue.dispatch(clock, record);
        check(order == std::vector<dok::OwnerId>({3, 1, 2}));
        check(queue.events(3)[0].deadline == 20);
        order.clear();
        now = 1000;
        queue.dispatch(clock, record);
        check(order.size() == 3 && queue.events(3)[0].deadline == 1010);
        queue.clear();
        check(queue.active_owners() == 0 && queue.events(1).empty());
        check(!queue.create(1) && !queue.create(0));
        check(!queue.cancel_current() && !queue.set_first_interval(5));

        // Native compaction copies BEFORE callback; the updated source is lost.
        now = 0;
        check(queue.schedule(1, 1, 10, now));
        check(queue.schedule(1, 2, 10, now));
        check(queue.schedule(1, 3, 20, now));
        check(!queue.schedule(1, 4, 10, now));
        now = 10;
        std::vector<dok::CallbackId> calls;
        queue.dispatch(clock, [&](dok::OwnerId, dok::CallbackId callback) {
            calls.push_back(callback);
            if (callback == 1) check(queue.cancel_current());
            else check(queue.set_first_interval(77));
        });
        check(calls == std::vector<dok::CallbackId>({1, 2}));
        check(queue.events(1).size() == 2);
        check(queue.events(1)[0].callback == 2 && queue.events(1)[0].deadline == 10);
        check(queue.events(1)[0].interval == 77 && queue.events(1)[1].interval == 20);
        queue.clear();

        // Current-owner append is live; activating an empty owner is deferred.
        now = 0;
        check(queue.schedule(1, 1, 0, now));
        calls.clear();
        queue.dispatch(clock, [&](dok::OwnerId id, dok::CallbackId callback) {
            check(id == 1);
            calls.push_back(callback);
            if (callback == 1) {
                check(queue.schedule(1, 2, 0, now));
                check(queue.schedule(2, 3, 0, now));
            }
            check(queue.cancel_current());
        });
        check(calls == std::vector<dok::CallbackId>({1, 2}));
        check(queue.active_owners() == 1 && queue.events(1).empty());
        order.clear();
        queue.dispatch(clock, record);
        check(order == std::vector<dok::OwnerId>({2}));
        queue.clear();

        // Empty removal shortens the live outer bound: B waits one sweep.
        for (auto id : {1u, 2u, 3u}) check(queue.schedule(id, 1, 0, now));
        order.clear();
        queue.dispatch(clock, [&](dok::OwnerId id, dok::CallbackId) {
            order.push_back(id);
            if (id == 3) check(queue.cancel_current());
        });
        check(order == std::vector<dok::OwnerId>({3, 1}));
        order.clear();
        queue.dispatch(clock, record);
        check(order == std::vector<dok::OwnerId>({2, 1}));
        queue.clear();

        // Unsigned native comparison deliberately preserves rollover behaviour.
        check(queue.schedule(1, 1, -1, 0));
        now = 0;
        calls.clear();
        queue.dispatch(clock, [&](dok::OwnerId, dok::CallbackId c) { calls.push_back(c); });
        check(calls.empty());
        now = 0xffffffffu;
        queue.dispatch(clock, [&](dok::OwnerId, dok::CallbackId c) { calls.push_back(c); });
        check(calls.size() == 1 && queue.events(1)[0].deadline == 0xfffffffeu);
        queue.clear();
        check(queue.schedule(1, 1, 10, 0xfffffffau));
        queue.dispatch(clock, [&](dok::OwnerId, dok::CallbackId) { now = 7; });
        check(queue.events(1)[0].deadline == 17); // Clock is read again after callback.
        queue.clear();

        // Destruction during dispatch never releases a live callback's slots.
        now = 0;
        check(queue.schedule(1, 1, 0, now));
        check(queue.schedule(1, 2, 0, now));
        calls.clear();
        queue.dispatch(clock, [&](dok::OwnerId id, dok::CallbackId callback) {
            calls.push_back(callback);
            check(queue.destroy(id));
            check(!queue.create(id));
            check(!queue.schedule(id, 3, 0, now));
            check(queue.schedule(2, 3, 0, now));
            check(queue.destroy(2)); // Delete a deferred activation too.
        });
        check(calls.size() == 1 && queue.active_owners() == 0);
        check(queue.create(1) && queue.create(2));
        check(queue.schedule(1, 1, 0, now));
        check(queue.schedule(1, 2, 0, now));
        calls.clear();
        queue.dispatch(clock, [&](dok::OwnerId, dok::CallbackId callback) {
            calls.push_back(callback);
            bool rejected = false;
            try { queue.dispatch(clock, record); } catch (const std::logic_error&) { rejected = true; }
            check(rejected);
            queue.clear();
            check(!queue.schedule(2, 1, 0, now));
        });
        check(calls.size() == 1 && queue.active_owners() == 0);
        check(queue.events(1).empty());
        check(queue.schedule(1, 1, 0, now));
        order.clear();
        queue.dispatch(clock, record);
        check(order.size() == 1);
        std::cout << "Native event order, mutation, wrap and lifetime vectors passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
