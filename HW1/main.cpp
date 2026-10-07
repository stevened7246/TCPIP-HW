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
        if (end != portText.size() || port < 1 || port > 65535)
            throw std::runtime_error("Invalid port");
        chat::Server server;
        std::exception_ptr failure;
        std::promise<void> started;
        auto ready = started.get_future();
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
        }
        server.stop();
        worker.join();
        if (failure)
            std::rethrow_exception(failure);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    WSACleanup();
    return result;
}
