// 使用者資料模型，保存登入暱稱。
#pragma once
#include <string>
namespace chat {
struct User {
    std::string name;
    // session 中空名稱代表尚未登入；此模型沒有密碼或永久帳號資料。
};
} // namespace chat
