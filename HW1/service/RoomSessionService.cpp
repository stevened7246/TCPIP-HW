// 管理連線登記、離線清理、名稱驗證與同房間廣播。
#include "ChatServices.h"
#include <algorithm>
namespace chat {
bool ChatServices::validName(const std::string &s) {
    // 使用明確 ASCII 範圍，不受系統 locale 影響；空名稱或超長名稱一律拒絕。
    return !s.empty() && s.size() <= 32 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
    });
}
bool ChatServices::requireLogin(const Session &s) {
    if (!s->user.name.empty())
        return true;
    error(s, "Login required");
    return false;
}
void ChatServices::attach(const Session &s) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.push_back(s);
}
void ChatServices::detach(const Session &s) {
    // 離線時釋放暱稱，讓之後的新連線可以再次使用。
    std::lock_guard<std::mutex> lock(mutex_);
    users_.remove(s->user.name);
    sessions_.erase(std::remove(sessions_.begin(), sessions_.end(), s), sessions_.end());
}
void ChatServices::interruptAll() {
    // 關機時主動關閉所有 socket，讓正在收不完整封包的 worker 也能退出。
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &s : sessions_)
        s->interrupt();
}
void ChatServices::broadcast(const std::string &room, const Packet &p) {
    // 呼叫端需已持有 mutex_；包含發送者在內，只送給同房間的已登入用戶。
    for (auto &s : sessions_)
        if (s->room == room && !s->user.name.empty())
            s->send(p);
}
} // namespace chat
