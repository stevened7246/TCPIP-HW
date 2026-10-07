#include "GuiConnection.h"
#include "FileStorage.h"
#include <ws2tcpip.h>
#include <stdexcept>

namespace chat {
GuiConnection::GuiConnection(HWND window, std::filesystem::path downloads)
    : window_(window), downloads_(std::move(downloads)) {}
GuiConnection::~GuiConnection() {
    stop();
}
void GuiConnection::cancel() {
    stopping_ = true;
    SOCKET socket = socket_.exchange(INVALID_SOCKET);
    if (socket != INVALID_SOCKET) {
        shutdown(socket, SD_BOTH);
        closesocket(socket); // Cancel blocking receive as well as send.
    }
    condition_.notify_all();
}
void GuiConnection::stop() {
    cancel();
    if (receiver_.joinable())
        receiver_.join();
    if (sender_.joinable())
        sender_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
    outgoing_.clear();
    events_.clear();
    outgoingBytes_ = 0;
}
void GuiConnection::start(std::string address, unsigned short port, std::string nickname) {
    stop();
    stopping_ = false;
    try {
        sender_ = std::thread(&GuiConnection::sendLoop, this);
        receiver_ = std::thread(&GuiConnection::receiveLoop, this, address, port, nickname);
    } catch (...) {
        stop();
        throw;
    }
}
void GuiConnection::emit(GuiEvent event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // File contents are saved on the receiver, not retained in the UI queue.
        if (events_.size() >= 512) {
            events_.clear();
            events_.push_back({GuiEvent::Failure, {}, "UI queue exceeded; connection closed"});
            cancel();
        } else
            events_.push_back(std::move(event));
    }
    PostMessageW(window_, NetworkEvent, 0, 0);
}
std::deque<GuiEvent> GuiConnection::drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::deque<GuiEvent> result;
    result.swap(events_);
    return result;
}
bool GuiConnection::send(Packet packet) {
    size_t bytes = 0;
    for (const auto &field : packet.fields)
        bytes += field.size();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || stopping_ || outgoing_.size() >= 32 ||
        outgoingBytes_ + bytes > 4 * 1024 * 1024)
        return false;
    outgoingBytes_ += bytes;
    outgoing_.push_back(std::move(packet));
    condition_.notify_one();
    return true;
}
void GuiConnection::sendLoop() {
    try {
        while (!stopping_) {
            Packet packet{};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock,
                                [&] { return stopping_ || (connected_ && !outgoing_.empty()); });
                if (stopping_)
                    break;
                packet = std::move(outgoing_.front());
                outgoing_.pop_front();
                for (const auto &field : packet.fields)
                    outgoingBytes_ -= field.size();
            }
            if (!sendPacket(socket_.load(), packet)) {
                if (!stopping_)
                    emit({GuiEvent::Failure, {}, "Send failed; connection closed"});
                cancel();
            }
        }
    } catch (const std::exception &e) {
        emit({GuiEvent::Failure, {}, e.what()});
        cancel();
    }
}
std::string GuiConnection::saveFile(const std::string &originalName, const std::string &content) {
    std::filesystem::create_directories(downloads_);
    static std::atomic<unsigned long> sequence{0};
    std::filesystem::path path;
    const auto extension = attachmentExtension(originalName);
    const std::wstring wideExtension(extension.begin(), extension.end());
    HANDLE file;
    do {
        path = downloads_ / (L"received_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                             std::to_wstring(++sequence) + wideExtension);
        file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    } while (file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create download file");
    DWORD written = 0;
    bool ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written,
                        nullptr) != FALSE;
    CloseHandle(file);
    if (!ok || written != content.size()) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("Cannot write complete download file");
    }
    return path.u8string();
}
void GuiConnection::receiveLoop(const std::string &address, unsigned short port,
                                const std::string &nickname) {
    try {
        SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCKET)
            throw std::runtime_error("Cannot create socket");
        socket_ = socket;
        if (stopping_) {
            cancel();
            emit({GuiEvent::Disconnected, {}, {}});
            return;
        }
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_port = htons(port);
        if (inet_pton(AF_INET, address.c_str(), &endpoint.sin_addr) != 1)
            throw std::runtime_error("Please enter the server IPv4 address (Tailscale or LAN)");
        u_long nonblocking = 1;
        ioctlsocket(socket, FIONBIO, &nonblocking);
        int result = connect(socket, reinterpret_cast<sockaddr *>(&endpoint), sizeof endpoint);
        if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
            throw std::runtime_error("Connection failed; check server and port");
        bool ready = result == 0;
        for (int attempt = 0; !ready && attempt < 50 && !stopping_; ++attempt) {
            fd_set writeSet, errors;
            FD_ZERO(&writeSet);
            FD_ZERO(&errors);
            FD_SET(socket, &writeSet);
            FD_SET(socket, &errors);
            timeval timeout{0, 100000};
            result = select(0, nullptr, &writeSet, &errors, &timeout);
            if (result == SOCKET_ERROR)
                throw std::runtime_error("Connection cancelled or failed");
            if (result > 0) {
                int error = 0, length = sizeof error;
                if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error),
                               &length) != 0 ||
                    error)
                    throw std::runtime_error("Connection refused; start chat_server first");
                ready = true;
            }
        }
        if (!ready || stopping_)
            throw std::runtime_error("Connection timed out or cancelled");
        nonblocking = 0;
        ioctlsocket(socket, FIONBIO, &nonblocking);
        DWORD timeout = 3000;
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char *>(&timeout),
                   sizeof timeout);
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout),
                   sizeof timeout);
        Packet response{};
        if (!sendPacket(socket, {PacketType::Login, {nickname}}) ||
            !receivePacket(socket, response))
            throw std::runtime_error("Login response not received");
        if (response.type != PacketType::Ok)
            throw std::runtime_error(response.fields.empty() ? "Login rejected"
                                                             : response.fields[0]);
        timeout = 300000;
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout),
                   sizeof timeout);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connected_ = true;
        }
        emit({GuiEvent::Connected, {}, {}});
        while (!stopping_ && receivePacket(socket, response)) {
            if (response.type == PacketType::File && response.fields.size() == 4) {
                try {
                    response.fields[3] = saveFile(response.fields[2], response.fields[3]);
                } catch (const std::exception &e) {
                    emit({GuiEvent::Failure, {}, e.what()});
                    continue;
                }
            }
            emit({GuiEvent::Incoming, std::move(response), {}});
        }
    } catch (const std::exception &e) {
        if (!stopping_)
            emit({GuiEvent::Failure, {}, e.what()});
    }
    cancel();
    emit({GuiEvent::Disconnected, {}, {}});
}
} // namespace chat
