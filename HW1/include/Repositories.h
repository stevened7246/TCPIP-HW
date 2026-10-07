// 宣告記憶體資料儲存介面；資料不會寫入磁碟。
#pragma once
#include "domain/Room.h"
#include "domain/Message.h"
#include <map>
#include <set>
#include <deque>
namespace chat {
// All repository access is serialized by ChatServices::mutex_.
class UserRepository {
    // set 同時負責暱稱查重及離線時移除，不保存 User 物件或密碼。
    std::set<std::string> users_;

  public:
    bool add(const std::string &);
    void remove(const std::string &);
};
class RoomRepository {
    // map 以房名排序；建立時已有 lobby，列表依 map 順序輸出。
    std::map<std::string, Room> rooms_{{"lobby", {"lobby"}}};

  public:
    bool add(const std::string &);
    bool contains(const std::string &) const;
    std::string list() const;
};
class MessageRepository {
    // 每間房各一個 deque，前端是最舊訊息；查詢回傳副本。
    std::map<std::string, std::deque<Message>> messages_;

  public:
    void add(const Message &);
    std::deque<Message> history(const std::string &) const;
};
} // namespace chat
