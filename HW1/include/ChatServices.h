// 宣告聊天室服務與請求入口；共用資料由 mutex_ 保護。
#pragma once
#include "Repositories.h"
#include "domain/ClientSession.h"
#include <memory>
#include <vector>
namespace chat {
using Session = std::shared_ptr<ClientSession>;
class ChatServices {
    std::mutex mutex_;
    // 保護使用者、房間、歷史訊息，以及 session 的暱稱與房名。
    // 目前持鎖時也會同步傳送；慢速接收者可能延後其他連線的服務處理。
    UserRepository users_;
    RoomRepository rooms_;
    MessageRepository messages_;
    std::vector<Session> sessions_;
    // shared_ptr 讓服務列表及 worker 可共同持有連線，直到雙方皆釋放。
    void broadcast(const std::string &, const Packet &);
    static void ok(const Session &s, const std::string &text) {
        s->send({PacketType::Ok, {text}});
    }
    static void error(const Session &s, const std::string &text) {
        s->send({PacketType::Error, {text}});
    }
    bool requireLogin(const Session &);
    // 尚未登入會送 Error 並回傳 false；呼叫端須持有服務鎖。

  public:
    static bool validName(const std::string &);
    // 暱稱與房名共用規則：1–32 個 ASCII 英數字、底線或連字號。
    void attach(const Session &);
    void detach(const Session &);
    void interruptAll();
    void login(const Session &, const Packet &);
    void room(const Session &, const Packet &);
    void message(const Session &, const Packet &);
    void file(const Session &, const Packet &);
};
void handleAuth(ChatServices &, const Session &, const Packet &);
void handleRoom(ChatServices &, const Session &, const Packet &);
void handleMessage(ChatServices &, const Session &, const Packet &);
void handleFile(ChatServices &, const Session &, const Packet &);
} // namespace chat
