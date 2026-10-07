// 保存單一用戶端的連線、暱稱與所在房間，並同步傳送及關閉操作。
#pragma once
#include "protocol/Packet.h"
#include "User.h"
#include <mutex>
namespace chat {
struct ClientSession {
    explicit ClientSession(SOCKET s) : socket(s) {}
    // Immutable handle: only the owning receiver reads it without sendMutex.
    const SOCKET socket;
    User user;
    std::string room;
    std::mutex sendMutex;
    bool closed = false; // guarded by sendMutex
    // user / room 由 ChatServices 的 mutex_ 保護；closed 由 sendMutex 保護。
    // socket 值不改變；close 以 closed 保證此連線只關閉一次。

    bool send(const Packet &packet) {
        // 同一 socket 的封包不可交錯傳送，整個封包共用一把傳送鎖。
        std::lock_guard<std::mutex> lock(sendMutex);
        if (closed)
            return false;
        if (sendPacket(socket, packet))
            return true;
        shutdown(socket, SD_BOTH);
        return false;
    }
    // Used only during server shutdown, after the listener has been closed.
    // closesocket cancels a pending blocking recv, including a partial frame.
    void interrupt() {
        close();
    }
    void close() {
        std::lock_guard<std::mutex> lock(sendMutex);
        if (!closed) {
            shutdown(socket, SD_BOTH);
            closesocket(socket);
            closed = true;
        }
    }
};
} // namespace chat
