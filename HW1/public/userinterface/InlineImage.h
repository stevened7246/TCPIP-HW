// 宣告聊天圖片預覽介面；呼叫端需先初始化 GDI+ 與 OLE。
#pragma once
#include <windows.h>
#include <filesystem>
namespace chat {
// Decode raster data and insert a bounded thumbnail; never execute an attachment.
// Caller initializes GDI+ and OLE on the UI thread.
// 回傳 false 表示格式、尺寸、解碼或插入失敗，可改顯示文字附件。
bool appendInlineImage(HWND transcript, const std::filesystem::path &path, int maxWidth,
                       int maxHeight, int dpi);
} // namespace chat
