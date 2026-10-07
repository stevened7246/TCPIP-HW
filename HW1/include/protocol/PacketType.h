// 定義雙方共用的封包編號；1–8 為請求，100–104 為回覆或廣播。
#pragma once
#include <cstdint>
namespace chat {
enum class PacketType : uint16_t {
    Login = 1,
    // 請求：登入 [暱稱]；列房、離房、歷史 []；建房與入房 [房名]。
    ListRooms = 2,
    CreateRoom = 3,
    JoinRoom = 4,
    LeaveRoom = 5,
    SendMessage = 6,
    History = 7,
    SendFile = 8,
    // 傳訊 [文字]；傳檔 [原始檔名, bytes]，各欄位仍附有 4-byte 長度。
    Ok = 100,
    Error = 101,
    Rooms = 102,
    Message = 103,
    File = 104
    // 回覆：Ok/Error [說明]；Rooms [換行房名]。
    // 廣播：Message [房間, 暱稱, 文字, 時間]；File [房間, 暱稱, 檔名, bytes]。
};
}
