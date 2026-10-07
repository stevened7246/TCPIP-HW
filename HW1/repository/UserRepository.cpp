// 以集合登記在線暱稱，避免不同連線使用相同名稱。
#include "Repositories.h"
namespace chat {
bool UserRepository::add(const std::string &name) {
    // 登入服務使用回傳值判斷暱稱是否被其他連線占用。
    return users_.insert(name).second;
}
void UserRepository::remove(const std::string &name) {
    users_.erase(name);
}
} // namespace chat
