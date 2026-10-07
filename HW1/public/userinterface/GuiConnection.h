// 宣告 GUI 背景網路連線與事件佇列，供 UI 執行緒取出結果。
#pragma once
#include "protocol/Packet.h"
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace chat {
constexpr UINT NetworkEvent = WM_APP + 1;
// 背景工作只送通知，不把事件指標放進訊息；UI 透過 drain 取出整批事件。
struct GuiEvent {
    enum Kind { Connected, Incoming, Failure, Disconnected } kind;
    Packet packet{PacketType::Ok, {}};
    std::string text;
};
class GuiConnection {
  public:
    explicit GuiConnection(HWND window, std::filesystem::path downloads);
    ~GuiConnection();
    void start(std::string address, unsigned short port, std::string nickname);
    void stop();
    bool send(Packet packet);
    std::deque<GuiEvent> drain();

  private:
    HWND window_;
    std::filesystem::path downloads_;
    std::atomic<SOCKET> socket_{INVALID_SOCKET};
    std::atomic<bool> stopping_{true};
    std::thread receiver_, sender_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool connected_ = false;
    // connected_、兩個佇列與 outgoingBytes_ 都由 mutex_ 保護。
    // socket_ 與 stopping_ 是 atomic，允許取消操作和背景執行緒共享狀態。
    std::deque<Packet> outgoing_;
    std::deque<GuiEvent> events_;
    size_t outgoingBytes_ = 0;
    void cancel();
    void emit(GuiEvent event);
    void receiveLoop(const std::string &, unsigned short, const std::string &);
    void sendLoop();
    std::string saveFile(const std::string &originalName, const std::string &content);
};
} // namespace chat
