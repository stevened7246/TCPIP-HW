// 檔案請求入口，將附件交給檔案服務驗證及廣播。
#include "ChatServices.h"
namespace chat {
void handleFile(ChatServices &services, const Session &session, const Packet &packet) {
    services.file(session, packet);
    // 此層不開啟本機檔案，內容直接取自封包，由 service 驗證後轉送。
}
} // namespace chat
