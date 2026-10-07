// 檢查附件大小與檔名，再轉送給同房間用戶；伺服器不保存附件。
#include "ChatServices.h"
namespace chat {
void ChatServices::file(const Session &s, const Packet &p) {
    // SendFile 請求：fields[0] 原始檔名，fields[1] 二進位內容。
    // 檔名上限是 128 bytes，附件上限是 1 MiB；空檔案可傳送。
    std::lock_guard<std::mutex> lock(mutex_);
    if (!requireLogin(s))
        return;
    if (s->room.empty())
        return error(s, "Join a room first");
    if (p.fields.size() != 2 || p.fields[0].empty() || p.fields[0].size() > 128 ||
        p.fields[1].size() > 1024 * 1024)
        return error(s, "File limit: 1 MiB; name limit: 128 bytes");
    for (unsigned char c : p.fields[0])
        // 拒絕控制字元、路徑分隔符與 Windows 檔名禁用字元。
        if (c < 32 || c == 127 || std::string("/\\:*?\"<>|").find(c) != std::string::npos)
            return error(s, "Unsafe filename");
    if (p.fields[0] == "." || p.fields[0] == "..")
        return error(s, "Unsafe filename");
    broadcast(s->room, {PacketType::File, {s->room, s->user.name, p.fields[0], p.fields[1]}});
    // File 廣播欄位依序為：房間、發送者、原始檔名、內容；不另回覆 Ok。
}
} // namespace chat
