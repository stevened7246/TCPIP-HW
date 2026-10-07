// 宣告 TCP 伺服器與停止介面，每條連線由獨立 worker 處理。
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
        // 只提出停止要求，run 的迴圈負責關 socket 與 join worker。
        stopping_ = true;
    }
};
} // namespace chat
