// 以房間為單位保存最近 100 則文字訊息，超出時刪除最舊一則。
#include "Repositories.h"
namespace chat {
void MessageRepository::add(const Message &m) {
    auto &q = messages_[m.room];
    q.push_back(m);
    // push_back 保持時間順序；達 101 則時移除最早一則，留下最新 100 則。
    if (q.size() > 100)
        q.pop_front();
}
std::deque<Message> MessageRepository::history(const std::string &room) const {
    auto it = messages_.find(room);
    return it == messages_.end() ? std::deque<Message>{} : it->second;
}
} // namespace chat
