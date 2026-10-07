// 定義記憶體中的封包與收送介面；fields 也能保存含 NUL 的二進位內容。
#pragma once
#include <winsock2.h>
#include <string>
#include <vector>
#include "PacketType.h"
#include "PacketHeader.h"
namespace chat {
struct Packet {
    // 這是程式內部模型，不能直接把 struct 記憶體送上網路。
    PacketType type;
    std::vector<std::string> fields;
};
bool receivePacket(SOCKET socket, Packet &packet);
// receivePacket：成功時填入完整封包；斷線、逾時或協定錯誤回傳 false。
// 失敗時 packet 可能已被部分改寫，呼叫端不可將它當作有效請求。
bool sendPacket(SOCKET socket, const Packet &packet);
// sendPacket：全部位元組送出才回傳 true；封包過大等編碼錯誤會拋出例外。
std::string encodePacket(const Packet &packet);
// encodePacket：產生「12-byte 標頭 + payload」，本身不操作 socket。
} // namespace chat
