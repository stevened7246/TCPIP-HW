#pragma once
#include "ChatServices.h"
#include <atomic>
#include <future>

namespace chat {
class Server {
    ChatServices services_;
    std::atomic<bool> stopping_{false};
    void serve(const Session &);

  public:
    void run(unsigned short port, const std::string &bindAddress, std::promise<void> &ready);
    void stop() {
        stopping_ = true;
    }
};
} // namespace chat
