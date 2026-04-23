#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H

#include <cstdint>
#include <cstring>
#include <atomic>
#include <memory>

static constexpr int MAX_PACKET_SIZE    = 1500;
static constexpr int MAX_SLOTS          = 65536;
static constexpr uint64_t INDEX_MASK = MAX_SLOTS - 1;

struct Slot {
    uint8_t data [MAX_PACKET_SIZE];
    int length;
};

class PacketQueue {
public:
    PacketQueue() : slots(new Slot[MAX_SLOTS]), write_position(0), read_position(0) {}

    // Push a pacaket into the queue
    // Call by the network thread.
    // Returns false if the queue is full
    bool push(const uint8_t* packet_data, int packet_length) {
        uint64_t write = write_position.load(std::memory_order_relaxed);
        uint64_t read = read_position.load(std::memory_order_acquire);

        if (write - read >= MAX_SLOTS) {
            return false;
        }

        Slot& target_slot = slots[write & INDEX_MASK];

        int copy_length = packet_length;
        if (packet_length > MAX_PACKET_SIZE) {
            copy_length = MAX_PACKET_SIZE;
        }

        std::memcpy(target_slot.data, packet_data, copy_length);
        target_slot.length = copy_length;

        write_position.store(write + 1, std::memory_order_release);
        return true;
    }

    // Pop a packet from the queue
    // Called by the writer thread.
    // Returns pointer to the slot, or nullptr if empty
    const Slot* pop() {
        uint64_t read = read_position.load(std::memory_order_relaxed);
        uint64_t write = write_position.load(std::memory_order_acquire);

        if (read >= write) {
            return nullptr;
        }

        const Slot* source_slot = &slots[read & INDEX_MASK];
        read_position.store(read + 1, std::memory_order_release);
        return source_slot;
    }

    // Number of Items currently in the queue
    uint64_t size() const {
        return write_position.load(std::memory_order_relaxed) -
               read_position.load(std::memory_order_relaxed);
    }

private:
    std::unique_ptr<Slot[]> slots;

    // Separate cache lines to prevent false sharing
    alignas(64) std::atomic<uint64_t> write_position;
    alignas(64) std::atomic<uint64_t> read_position;
};

#endif
