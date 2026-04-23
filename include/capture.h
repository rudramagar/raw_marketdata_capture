#ifndef CAPTURE_H
#define CAPTURE_H

#include "config_parser.h"
#include "socket.h"
#include "packet_queue.h"

#include <atomic>
#include <string>

class Capture {
public:
    // Start Capture
    // Blocks until stop() is called
    bool start(const ServiceConfig& config, const std::string& output_dir);

    // Signal both thread
    // to stop gracefully
    void stop();

private:
    // Network thread: recv from
    // sockets -> push to queue
    void network_loop();

    // Writer thread:
    // pop from queue -> write to disk
    void writer_loop();

    PacketQueue         packet_queue;
    Socket              sockets[2];
    int                 socket_count = 0;
    std::string         output_path;
    int                 output_fd = -1;
    std::atomic<bool>   running{false};
};

#endif
