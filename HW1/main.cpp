// 伺服器入口：初始化 Winsock、解析連接埠，並等待 /quit 或輸入結束。
#include "Server.h"
#include <iostream>
#include <thread>
int main(int argc, char **argv) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        return 1;
    int result = 0;
    try {
        std::string portText = argc > 1 ? argv[1] : "9000";
        size_t end = 0;
        int port = std::stoi(portText, &end);
        // end 檢查整個參數皆被解析，拒絕 9000abc 這類只前段是數字的輸入。
        if (end != portText.size() || port < 1 || port > 65535)
            throw std::runtime_error("Invalid port");
        chat::Server server;
        std::exception_ptr failure;
        std::promise<void> started;
        auto ready = started.get_future();
        // 等監聽成功再讀控制指令；啟動失敗會透過 future 傳回主執行緒。
        std::thread worker([&] {
            try {
                server.run(static_cast<unsigned short>(port), argc > 2 ? argv[2] : "0.0.0.0", 
                           started);
            } catch (...) {
                failure = std::current_exception();
                try {
                    started.set_exception(failure);
                } catch (const std::future_error &) {
                }
            }
        });
        try {
            ready.get();
        } catch (...) {
            worker.join();
            throw;
        }
        std::string line;
        while (std::getline(std::cin, line) && line != "/quit") {
            // 其他輸入忽略；stdin 結束也會走正常停止程序。
        }
        server.stop();
        worker.join();
        // 等待網路執行緒結束後再讀 failure，避免跨執行緒同時讀寫。
        if (failure)
            std::rethrow_exception(failure);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    WSACleanup();
    return result;
}
