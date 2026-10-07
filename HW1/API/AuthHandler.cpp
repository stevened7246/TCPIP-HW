// 登入請求入口，將封包交給登入服務檢查。
#include "ChatServices.h"
namespace chat {
void handleAuth(ChatServices &services, const Session &session, const Packet &packet) {
    services.login(session, packet);
    // handler 不自行修改 session，由 service 統一驗證並回覆 Ok 或 Error。
}
} // namespace chat
