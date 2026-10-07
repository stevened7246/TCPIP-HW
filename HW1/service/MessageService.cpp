// 處理房間文字訊息與歷史查詢，時間以 Unix 秒數記錄。
#include "ChatServices.h"
#include <chrono>
namespace chat {
void ChatServices::message(const Session &session, const Packet &packet) {
    // SendMessage 帶一個文字欄位；History 不帶欄位，兩者都需登入並加入房間。
    std::lock_guard<std::mutex> lock(mutex_);
    if (!requireLogin(session))
        return;
    if (session->room.empty())
        return error(session, "Join a room first");
    if (packet.type == PacketType::History) {
        // 歷史逐則回傳，最後的 Ok 表示本次查詢結束。
        if (!packet.fields.empty())
            return error(session, "History takes no fields");
        for (const auto &message : messages_.history(session->room))
            session->send({PacketType::Message,
                           {message.room, message.sender, message.text, message.timestamp}});
        return ok(session, "End of history");
    }
    if (packet.fields.size() != 1 || packet.fields[0].empty() || packet.fields[0].size() > 4096)
        return error(session, "Message must contain 1-4096 bytes");
    // 傳輸時間使用自 Epoch 起的秒數，不是本地格式化時間。
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    Message message{session->room, session->user.name, packet.fields[0], std::to_string(timestamp)};
    // Message 廣播欄位固定為 [房間, 發送者, 文字, Unix 秒數字串]。
    messages_.add(message);
    // 新訊息也回送發送者，讓用戶端依伺服器廣播顯示聊天內容。
    broadcast(session->room, {PacketType::Message,
                              {message.room, message.sender, message.text, message.timestamp}});
}
} // namespace chat
