#include "capture.h"

#include <thread>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <syslog.h>

bool Capture::start(const ServiceConfig& config, const std::string& output_path) {

    // Join SSM
    socket_count = 0;
    for (size_t i = 0; i < config.channels.size() && i < 2; i++) {
        const ChannelConfig& ch = config.channels[i];

        if (!sockets[i].join_multicast_channel(ch.multicast_address, ch.multicast_port, ch.interface, ch.source)) {
            syslog(LOG_ERR, "join failed  %s:%u\n", ch.multicast_address.c_str(), ch.multicast_port);
            return false;
        }

        sockets[i].set_recv_buffer(config.queue_size);
        socket_count++;
        syslog(LOG_INFO, "joined %s:%u\n", ch.multicast_address.c_str(), ch.multicast_port);
    }

    if (socket_count == 0) {
        return false;
    }

    // Build filename
    // Open output file
    output_fd = ::open(output_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (output_fd < 0) {
        syslog(LOG_ERR, "cannot open %s", output_path.c_str());
        return false;
    }

    // Start Threads
    running = true;
    std::thread net_thread(&Capture::network_loop, this);
    std::thread wrt_thread(&Capture::writer_loop, this);

    net_thread.join();
    wrt_thread.join();

    // Cleanup
    ::close(output_fd);
    output_fd = -1;
    for (int i = 0; i < socket_count; i++) {
        sockets[i].close();
    }

    syslog(LOG_INFO, "stopped");
    return true;
}

void Capture::stop() {
    running = false;
}

void Capture::network_loop() {
    struct pollfd poll_fds[2];
    for (int i = 0; i < socket_count; i++) {
        poll_fds[i].fd = sockets[i].fd();
        poll_fds[i].events = POLLIN;
    }

    uint8_t recv_buffer[MAX_PACKET_SIZE];
    time_t last_log_time = ::time(nullptr);

    while (running) {
        int ready = ::poll(poll_fds, socket_count, 100);

        // syslog
        time_t now = ::time(nullptr);
        if (now != last_log_time) {
            syslog(LOG_INFO, "received=%llu dropped=%llu written=%llu",
                    (unsigned long long)total_received.load(),
                    (unsigned long long)total_dropped.load(),
                    (unsigned long long)total_written.load());

            last_log_time = now;
        }

        if (ready <= 0) {
            continue;
        }

        for (int i = 0; i < socket_count; i++) {
            if (!(poll_fds[i].revents & POLLIN)) {
                continue;
            }

            int bytes = sockets[i].recv(recv_buffer, MAX_PACKET_SIZE);
            if (bytes <= 0) {
                continue;
            }

            // MoldUDP64 message count
            // at bytes 18-19 (big-endian)
            uint16_t msg_count = (recv_buffer[18] << 8) | recv_buffer[19];

            // Skip heartbeats
            // message_count == 0
            if (msg_count == 0) {
                continue;
            }

            if (!packet_queue.push(recv_buffer, bytes)) {
                total_dropped++;
            }

            total_received++;
        }
    }

    if (total_dropped > 0) {
        syslog(LOG_WARNING, "dropped %llu packets (queue full)",
                (unsigned long long)total_dropped);
    }
}

void Capture::writer_loop() {

    while (running || packet_queue.size() > 0) {
        const Slot* slot = packet_queue.pop();
        if (slot == nullptr) {
            continue;
        }

        uint32_t packet_length = (uint32_t)slot->length;

        if (::write(output_fd, &packet_length, sizeof(packet_length)) < 0) {
            continue;
        }

        if (::write(output_fd, slot->data, slot->length) < 0) {
            continue;
        }

        total_written++;
    }
}


