// 檢查暱稱格式與重複登入，成功後自動進入 lobby。
#include "ChatServices.h"
namespace chat {
void ChatServices::login(const Session &s, const Packet &p) {
    // Login 的 fields[0] 是暱稱；只接受一個欄位，且同一連線不能重複登入。
    // 鎖涵蓋「檢查及登記暱稱」，避免兩條連線同時取得相同名稱。
    std::lock_guard<std::mutex> lock(mutex_);
    if (p.fields.size() != 1 || !validName(p.fields[0]))
        return error(s, "Nickname: 1-32 ASCII letters, digits, _ or -");
    if (!s->user.name.empty())
        return error(s, "Already logged in");
    if (!users_.add(p.fields[0]))
        // set::insert 的結果代表是否新增成功，重複名稱會回覆 Error。
        return error(s, "Nickname already in use");
    s->user.name = p.fields[0];
    s->room = "lobby";
    // 登入成功同時設定房間，後續即可送訊息，不需要再送 JoinRoom。
    ok(s, "Logged in; joined lobby");
}
} // namespace chat
