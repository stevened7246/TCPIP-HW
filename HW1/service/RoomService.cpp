// 處理房間列表、建立、加入與離開；建立後仍需另外加入。
#include "ChatServices.h"
namespace chat {
void ChatServices::room(const Session &s, const Packet &p) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!requireLogin(s))
        return;
    const bool needsName = p.type == PacketType::CreateRoom || p.type == PacketType::JoinRoom;
    // CreateRoom / JoinRoom 需要 fields[0] 房名，其餘房間請求不得附帶欄位。
    if (p.fields.size() != (needsName ? 1u : 0u))
        return error(s, "Invalid room request");
    if (needsName && !validName(p.fields[0]))
        return error(s, "Invalid room name");
    switch (p.type) {
    case PacketType::ListRooms:
        // Rooms 回覆只有一個欄位，各房名以換行分隔，並非每間各一個欄位。
        s->send({PacketType::Rooms, {rooms_.list()}});
        break;
    case PacketType::CreateRoom:
        // 只新增房間；不改變目前房間，也不自動把建立者加入新房間。
        if (!rooms_.add(p.fields[0]))
            return error(s, "Room exists or room limit reached");
        ok(s, "Room created; use /join to enter");
        break;
    case PacketType::JoinRoom:
        // 直接替換 session 的房名；每條連線同一時間只屬於一間房。
        if (!rooms_.contains(p.fields[0]))
            return error(s, "Room not found");
        s->room = p.fields[0];
        ok(s, "Joined " + s->room);
        break;
    case PacketType::LeaveRoom:
        // 空房名代表未加入房間，保留登入狀態但不可傳訊息或附件。
        s->room.clear();
        ok(s, "Left room");
        break;
    default:
        error(s, "Invalid room request");
    }
    
}
} // namespace chat
