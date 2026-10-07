// 房間資料模型，保存房間名稱。
#pragma once
#include <string>
namespace chat {
struct Room {
    std::string name;
    // 模型不保存成員；房間成員由各 ClientSession 的 room 欄位決定。
};
} // namespace chat
