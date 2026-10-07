// 將 TCP 位元組串流還原為完整封包，驗證標頭與欄位長度。
#include "protocol/Packet.h"
#include <cstring>
namespace chat {
static bool receiveExact(SOCKET socket, char *buffer, size_t size) {
    // TCP 一次 recv 不一定收齊，持續讀取直到指定長度或斷線。
    while (size) {
        int value = recv(socket, buffer, static_cast<int>(size), 0);
        if (value <= 0)
            return false;
        buffer += value;
        size -= value;
    }
    return true;
}
static uint32_t readUint32(const char *buffer) {
    // memcpy 避免未對齊指標讀取，ntohl 再轉回主機的整數位元組序。
    uint32_t value;
    std::memcpy(&value, buffer, 4);
    return ntohl(value);
}
static uint16_t readUint16(const char *buffer) {
    uint16_t value;
    std::memcpy(&value, buffer, 2);
    return ntohs(value);
}
bool receivePacket(SOCKET socket, Packet &packet) {
    char header[HeaderSize];
    if (!receiveExact(socket, header, sizeof header))
        return false;
    auto size = readUint32(header + 8);
    // 先驗證標頭及大小，再配置 payload，避免依不合法長度配置記憶體。
    if (readUint32(header) != Magic || readUint16(header + 4) != Version || size > MaxPayload)
        return false;
    std::string body(size, '\0');
    if (!receiveExact(socket, body.data(), size))
        return false;
    packet.type = static_cast<PacketType>(readUint16(header + 6));
    // 解析層不決定類型是否可處理；未知類型由 Server 分派時回覆 Error。
    packet.fields.clear();
    size_t offset = 0;
    while (offset < body.size()) {
        // 每個欄位先讀 4-byte 長度，再讀內容；必須落在 payload 範圍內。
        if (body.size() - offset < 4 || packet.fields.size() >= 16)
            return false;
        auto fieldSize = readUint32(body.data() + offset);
        offset += 4;
        if (fieldSize > body.size() - offset)
            return false;
        packet.fields.push_back(body.substr(offset, fieldSize));
        offset += fieldSize;
    }
    return true;
}
} // namespace chat
