// 房間請求入口，轉交列表、建立、加入與離開操作。
#include "ChatServices.h"
namespace chat {
void handleRoom(ChatServices &services, const Session &session, const Packet &packet) {
    services.room(session, packet);
    // 多種房間操作共用入口，service 再依 packet.type 判斷欄位及行為。
}
} // namespace chat
