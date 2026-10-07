// 定義協定識別碼、版本與大小上限；網路標頭固定為 12 bytes。
#pragma once
#include <cstdint>
namespace chat {
constexpr uint32_t Magic = 0x43484154;
// 0–3: CHAT 識別碼；4–5: 版本；6–7: 類型；8–11: payload 長度。
// 長度只計算 payload，不包含標頭；所有多位元組整數皆為 big endian。
constexpr uint16_t Version = 1;
constexpr uint32_t MaxPayload = 1024 * 1024 + 4096;
constexpr unsigned HeaderSize = 12;
// Network byte order: magic:u32, version:u16, type:u16, payload size:u32.
} // namespace chat
