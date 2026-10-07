// 訊息請求入口，處理新訊息與歷史紀錄的服務轉交。
#include "ChatServices.h"
namespace chat {
void handleMessage(ChatServices &services, const Session &session, const Packet &packet) {
    services.message(session, packet);
    // SendMessage 與 History 共用此入口，欄位驗證及回覆順序由 service 決定。
}
} // namespace chat
