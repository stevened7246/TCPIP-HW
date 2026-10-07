// 命令列用戶端：主執行緒讀取指令，接收執行緒顯示訊息與保存附件。
#include "protocol/Packet.h"
#include "FileStorage.h"
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <mutex>
using namespace chat;
int main(int argc, char **argv) {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) {
        std::cout << "Usage: chat_client nickname [host=127.0.0.1] [port=9000]\n";
        return 1;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return 1;
    addrinfo hints{}, *addresses = nullptr;
    hints.ai_family = AF_INET;
    // 只連 IPv4；getaddrinfo 可以解析主機名，再逐一嘗試可用位址。
    hints.ai_socktype = SOCK_STREAM;
    SOCKET socketHandle = INVALID_SOCKET;
    if (!getaddrinfo(argc > 2 ? argv[2] : "127.0.0.1", argc > 3 ? argv[3] : "9000", &hints,
                     &addresses)) {
        for (auto a = addresses; a; a = a->ai_next) {
            socketHandle = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (socketHandle != INVALID_SOCKET &&
                connect(socketHandle, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0)
                break;
            if (socketHandle != INVALID_SOCKET)
                closesocket(socketHandle);
            socketHandle = INVALID_SOCKET;
        }
        freeaddrinfo(addresses);
    }
    if (socketHandle == INVALID_SOCKET) {
        std::cerr << "Connection failed\n";
        WSACleanup();
        return 1;
    }
    DWORD timeout = 3000;
    setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout),
               sizeof timeout);
    sendPacket(socketHandle, {PacketType::Login, {argv[1]}});
    std::atomic<bool> running{true};
    std::mutex output;
    // running 讓接收端通知主迴圈停止，output 防止兩條執行緒交錯輸出文字。
    std::thread receiver([&] {
        Packet p{};
        unsigned long counter = 0;
        while (receivePacket(socketHandle, p)) {
            std::lock_guard<std::mutex> lock(output);
            if (p.type == PacketType::Message && p.fields.size() == 4)
                std::cout << "[" << p.fields[0] << "] " << p.fields[1] << ": " << p.fields[2]
                          << '\n';
            else if (p.type == PacketType::File && p.fields.size() == 4) {
                try {
                    // Generated local filenames prevent traversal, device names and overwrite.
                    std::filesystem::create_directories("downloads");
                    std::string path;
                    do {
                        path = "downloads/received_" + std::to_string(GetCurrentProcessId()) + "_" +
                               std::to_string(++counter) + attachmentExtension(p.fields[2]);
                    } while (std::filesystem::exists(path));
                    std::ofstream file(path, std::ios::binary);
                    file.write(p.fields[3].data(),
                               static_cast<std::streamsize>(p.fields[3].size()));
                    file.close();
                    if (!file)
                        throw std::runtime_error("write failed");
                    std::cout << "File from " << p.fields[1] << ": " << p.fields[2] << " -> "
                              << path << '\n';
                } catch (const std::exception &e) {
                    std::cerr << "Cannot save file: " << e.what() << '\n';
                }
            } else
                for (const auto &f : p.fields)
                    std::cout << (p.type == PacketType::Error ? "Error: " : "") << f << '\n';
            std::cout.flush();
        }
        running = false;
        std::lock_guard<std::mutex> lock(output);
        std::cout << "Disconnected. Press Enter to exit.\n";
    });
    std::cout << "/rooms | /create name | /join name | /leave | /history | /file path | "
                 "/quit\nType text to chat.\n";
    std::string line;
    while (running && std::getline(std::cin, line)) {
        if (!running || line == "/quit")
            break;
        Packet p{PacketType::SendMessage, {line}};
        // 預設將輸入當聊天文字；符合已知 / 指令時改成對應封包。
        if (line == "/rooms")
            p = {PacketType::ListRooms, {}};
        else if (line == "/leave")
            p = {PacketType::LeaveRoom, {}};
        else if (line == "/history")
            p = {PacketType::History, {}};
        else if (line.rfind("/create ", 0) == 0)
            p = {PacketType::CreateRoom, {line.substr(8)}};
        else if (line.rfind("/join ", 0) == 0)
            p = {PacketType::JoinRoom, {line.substr(6)}};
        else if (line.rfind("/file ", 0) == 0) {
            try {
                auto path = std::filesystem::u8path(line.substr(6));
                std::ifstream file(path, std::ios::binary | std::ios::ate);
                // ate 先定位檔尾取得大小，配置內容後 seekg(0) 回到檔頭讀取。
                if (!file || file.tellg() < 0 || file.tellg() > 1024 * 1024)
                    throw std::runtime_error("File missing or larger than 1 MiB");
                std::string content(static_cast<size_t>(file.tellg()), '\0');
                file.seekg(0);
                if (!file.read(content.data(), static_cast<std::streamsize>(content.size())))
                    throw std::runtime_error("File read failed");
                p = {PacketType::SendFile, {path.filename().u8string(), content}};
            } catch (const std::exception &e) {
                std::lock_guard<std::mutex> lock(output);
                std::cerr << e.what() << '\n';
                continue;
            }
        }
        try {
            if (!sendPacket(socketHandle, p))
                break;
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> lock(output);
            std::cerr << e.what() << '\n';
        }
    }
    shutdown(socketHandle, SD_BOTH);
    // 先中斷 socket，喚醒接收執行緒，再 join；直接 join 可能卡在 recv。
    closesocket(socketHandle);
    receiver.join();
    WSACleanup();
    return 0;
}
