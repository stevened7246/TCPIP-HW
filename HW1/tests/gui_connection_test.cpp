// 測試 GUI 網路層的事件、訊息與檔案收送，不操作可見聊天視窗。
#include "public/userinterface/GuiConnection.h"
#include "FileStorage.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace chat;
using Clock = std::chrono::steady_clock;
template <class Predicate> GuiEvent awaitEvent(GuiConnection &connection, Predicate predicate) {
    // 輪詢事件佇列直到指定結果出現；設期限避免失敗時永久等待。
    auto deadline = Clock::now() + std::chrono::seconds(6);
    while (Clock::now() < deadline) {
        for (auto &event : connection.drain())
            if (predicate(event))
                return event;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("GUI transport event timed out");
}
void require(bool ok, const char *text) {
    if (!ok)
        throw std::runtime_error(text);
}
int main(int argc, char **argv) {
    // 參數由 Python 整合測試提供：伺服器連接埠、暫時下載目錄。
    if (argc != 3)
        return 2;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return 2;
    int result = 0;
    try {
        require(attachmentExtension("image.PNG") == ".png", "PNG extension");
        require(attachmentExtension("archive.tar.gz") == ".gz", "compound extension");
        require(attachmentExtension("README") == ".bin", "no extension fallback");
        require(attachmentExtension("image.png:stream") == ".bin", "unsafe extension fallback");
        require(attachmentExtension("image.png/evil") == ".bin", "path extension fallback");
        auto port = static_cast<unsigned short>(std::stoi(argv[1]));
        GuiConnection connection(nullptr, std::filesystem::u8path(argv[2]));
        connection.start("127.0.0.1", port, "GuiTransport");
        awaitEvent(connection, [](const GuiEvent &e) { return e.kind == GuiEvent::Connected; });
        require(connection.send({PacketType::SendMessage, {"GUI transport message"}}),
                "enqueue message");
        auto message = awaitEvent(connection, [](const GuiEvent &e) {
            return e.kind == GuiEvent::Incoming && e.packet.type == PacketType::Message;
        });
        require(message.packet.fields.at(2) == "GUI transport message", "message mismatch");
        std::string binary(1024 * 1024, '\0');
        for (size_t i = 0; i < binary.size(); ++i)
            binary[i] = static_cast<char>(i % 256);
        require(connection.send({PacketType::SendFile, {"test.PNG", binary}}), "enqueue file");
        auto file = awaitEvent(connection, [](const GuiEvent &e) {
            return e.kind == GuiEvent::Incoming && e.packet.type == PacketType::File;
        });
        require(std::filesystem::u8path(file.packet.fields.at(3)).extension() == L".png",
                "received extension missing");
        std::ifstream saved(std::filesystem::u8path(file.packet.fields.at(3)), std::ios::binary);
        std::string received((std::istreambuf_iterator<char>(saved)),
                             std::istreambuf_iterator<char>());
        require(received == binary, "saved file mismatch");
        GuiConnection duplicate(nullptr, std::filesystem::u8path(argv[2]));
        duplicate.start("127.0.0.1", port, "GuiTransport");
        auto rejection =
            awaitEvent(duplicate, [](const GuiEvent &e) { return e.kind == GuiEvent::Failure; });
        require(rejection.text.find("already in use") != std::string::npos,
                "duplicate nickname rejection");
        duplicate.stop();
        auto before = Clock::now();
        connection.stop();
        require(Clock::now() - before < std::chrono::seconds(2), "idle disconnect blocked");
        connection.start("127.0.0.1", port, "GuiReconnect");
        awaitEvent(connection, [](const GuiEvent &e) { return e.kind == GuiEvent::Connected; });
        connection.stop();
        connection.start("invalid", port, "GuiInvalid");
        awaitEvent(connection, [](const GuiEvent &e) { return e.kind == GuiEvent::Failure; });
        connection.stop();
        std::cout << "GUI transport: message, 1 MiB file, rejection, disconnect, reconnect, "
                     "invalid address passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    WSACleanup();
    return result;
}
