// 監聽 IPv4 TCP 連線，分派封包並管理最多 64 個連線 worker。
#include "Server.h"
#include <ws2tcpip.h>
#include <thread>
#include <iostream>
#include <stdexcept>
namespace chat {
void Server::serve(const Session &session) {
    // 此函式由該連線的 worker 執行；每次只處理一個已收齊的請求。
    try {
        Packet packet{};
        while (!stopping_ && receivePacket(session->socket, packet)) {
            switch (packet.type) {
            case PacketType::Login:
                handleAuth(services_, session, packet);
                break;
            case PacketType::ListRooms:
            case PacketType::CreateRoom:
            case PacketType::JoinRoom:
            case PacketType::LeaveRoom:
                handleRoom(services_, session, packet);
                break;
            case PacketType::SendMessage:
            case PacketType::History:
                handleMessage(services_, session, packet);
                break;
            case PacketType::SendFile:
                handleFile(services_, session, packet);
                break;
            default:
                session->send({PacketType::Error, {"Unknown packet type"}});
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Client error: " << e.what() << '\n';
    }
    services_.detach(session);
    // 無論正常離線、協定錯誤或例外，都移除服務登記並釋放 socket。
    session->close();
}
void Server::run(unsigned short port, const std::string &bindAddress, std::promise<void> &started) {
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
        throw std::runtime_error("socket failed");
    BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char *>(&exclusive), sizeof exclusive);
    sockaddr_in bindEndpoint{};
    bindEndpoint.sin_family = AF_INET;
    bindEndpoint.sin_port = htons(port);
    if (inet_pton(AF_INET, bindAddress.c_str(), &bindEndpoint.sin_addr) != 1) {
        closesocket(listener);
        throw std::runtime_error("Invalid bind IPv4 address");
    }
    if (bind(listener, reinterpret_cast<sockaddr *>(&bindEndpoint), sizeof bindEndpoint) ==
            SOCKET_ERROR ||
        listen(listener, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listener);
        throw std::runtime_error("bind/listen failed (port in use?)");
    }
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::vector<Worker> workers;
    started.set_value();
    // bind 與 listen 已成功，通知主執行緒可以開始接受 /quit 控制指令。
    std::cout << "Listening on " << bindAddress << ":" << port << "; enter /quit to stop."
              << std::endl;
    try {
        while (!stopping_) {
            for (auto it = workers.begin(); it != workers.end();) {
                // 已結束的 worker 先 join 再移除，回收執行緒與連線名額。
                if (*it->done) {
                    it->thread.join();
                    it = workers.erase(it);
                } else
                    ++it;
            }
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(listener, &readable);
            timeval timeout{0, 200000};
            // 每 200 ms 檢查停止旗標，避免 accept 永久阻塞關機流程。
            int ready = select(0, &readable, nullptr, nullptr, &timeout);
            if (ready == SOCKET_ERROR)
                throw std::runtime_error("select failed");
            if (!ready)
                continue;
            SOCKET peer = accept(listener, nullptr, nullptr);
            if (peer == INVALID_SOCKET)
                continue;
            if (workers.size() >= 64) {
                closesocket(peer);
                continue;
            }
            DWORD sendTimeout = 3000, receiveTimeout = 300000;
            // Winsock 逾時單位是毫秒：送出 3 秒、單次接收等待 5 分鐘。
            setsockopt(peer, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&sendTimeout),
                       sizeof sendTimeout);
            setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char *>(&receiveTimeout), sizeof receiveTimeout);
            auto session = std::make_shared<ClientSession>(peer);
            auto done = std::make_shared<std::atomic<bool>>(false);
            services_.attach(session);
            try {
                workers.push_back({std::thread{}, done});
                workers.back().thread = std::thread([this, session, done] {
                    serve(session);
                    *done = true;
                });
            } catch (...) {
                services_.detach(session);
                session->close();
                if (!workers.empty() && !workers.back().thread.joinable())
                    workers.pop_back();
                throw;
            }
        }
    } catch (...) {
        stopping_ = true;
        closesocket(listener);
        services_.interruptAll();
        for (auto &worker : workers)
            worker.thread.join();
        throw;
    }
    closesocket(listener);
    // 關閉用戶端 socket 以喚醒阻塞中的 recv，再等待 worker 結束。
    services_.interruptAll();
    for (auto &worker : workers)
        worker.thread.join();
}
} // namespace chat
