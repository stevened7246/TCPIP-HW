// 訊息資料模型；timestamp 保存 Unix 秒數的字串。
#pragma once
#include <string>
namespace chat {
struct Message {
    std::string room, sender, text, timestamp;
    // room 決定歷史所屬房間；sender 是發送時暱稱，離線後仍保留於歷史。
};
} // namespace chat
