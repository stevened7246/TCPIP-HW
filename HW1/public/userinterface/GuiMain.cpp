// Win32 GUI 入口與視窗事件處理，負責版面、操作與聊天內容顯示。
#include "GuiConnection.h"
#include "InlineImage.h"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <gdiplus.h>
#include <objidl.h>
#include <shellapi.h>
#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

using namespace chat;
namespace {
constexpr COLORREF Ink = RGB(235, 232, 238), Muted = RGB(153, 153, 169);
constexpr COLORREF Surface = RGB(24, 25, 34), Accent = RGB(196, 48, 73);
enum Control {
    Nick = 101,
    Host,
    Port,
    Connect,
    Rooms,
    Refresh,
    Join,
    Leave,
    RoomName,
    Create,
    Transcript,
    Input,
    Send,
    File,
    History,
    Downloads
};
std::wstring wide(const std::string &text) {
    int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        length);
    return result;
}
std::string utf8(const std::wstring &text) {
    int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                     nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        length, nullptr, nullptr);
    return result;
}
std::wstring value(HWND control) {
    int count = GetWindowTextLengthW(control);
    std::wstring result(count + 1, L'\0');
    GetWindowTextW(control, result.data(), count + 1);
    result.resize(count);
    return result;
}
std::filesystem::path executableDirectory() {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}
struct App {
    HWND window{}, controls[128]{};
    HFONT font{}, small{}, title{};
    HBRUSH brush = CreateSolidBrush(Surface);
    std::unique_ptr<GuiConnection> network;
    IStream *imageStream = nullptr;
    std::unique_ptr<Gdiplus::Bitmap> background;
    std::filesystem::path downloads = executableDirectory() / L"downloads";
    bool connected = false, busy = false, composing = false;
    std::wstring nickname, currentRoom = L"尚未加入", status = L"離線 · 輸入暱稱後連線";
    float scale = 1;
    int width = 1200, height = 780, chatX = 252, chatW = 640;
    HWND get(int id) const {
        return controls[id - 100];
    }
    int px(int logical) const {
        // 版面使用邏輯尺寸，繪製與配置時轉為目前 DPI 的像素。
        return static_cast<int>(logical * scale);
    }
    ~App() {
        // 先停止網路執行緒，再釋放視窗使用的圖片、字型與筆刷。
        network.reset();
        background.reset();
        if (imageStream)
            imageStream->Release();
        DeleteObject(font);
        DeleteObject(small);
        DeleteObject(title);
        DeleteObject(brush);
    }
    void place(int id, int x, int y, int w, int h) {
        MoveWindow(get(id), px(x), px(y), px(w), px(h), TRUE);
    }
    HWND add(int id, const wchar_t *klass, const wchar_t *text, DWORD style) {
        HWND control = CreateWindowExW(
            0, klass, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (!control)
            throw std::runtime_error("Cannot create GUI control");
        controls[id - 100] = control;
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return control;
    }
    void label(HDC dc, const std::wstring &text, int x, int y, int w, int h, HFONT face,
               COLORREF color) {
        SelectObject(dc, face);
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        RECT rect{px(x), px(y), px(x + w), px(y + h)};
        DrawTextW(dc, text.c_str(), -1, &rect,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    void append(const std::wstring &text, COLORREF color = Ink) {
        // 聊天紀錄過長時清理舊內容，避免 RichEdit 持續累積。
        HWND log = get(Transcript);
        if (GetWindowTextLengthW(log) > 100000) {
            SendMessageW(log, EM_SETSEL, 0, 20000);
            SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
        }
        SendMessageW(log, EM_SETSEL, static_cast<WPARAM>(-1), -1);
        CHARFORMAT2W format{};
        format.cbSize = sizeof format;
        format.dwMask = CFM_COLOR;
        format.crTextColor = color;
        SendMessageW(log, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        std::wstring line = text + L"\r\n\r\n";
        SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
        SendMessageW(log, EM_SCROLLCARET, 0, 0);
    }
    void updateControls() {
        // 連線中或已連線時鎖住連線參數；送訊息按鈕還需確認已加入房間。
        for (int id : {Nick, Host, Port})
            EnableWindow(get(id), !busy && !connected);
        for (int id : {Rooms, Refresh, Join, Leave, RoomName, Create})
            EnableWindow(get(id), connected);
        for (int id : {Input, Send, File, History})
            EnableWindow(get(id), connected && !currentRoom.empty());
        SetWindowTextW(get(Connect), connected ? L"中斷連線" : busy ? L"取消連線" : L"連線聊天室");
        InvalidateRect(window, nullptr, FALSE);
    }
    bool submit(Packet packet) {
        // true 只表示成功排入傳送佇列，尚不代表伺服器已接受或收到。
        if (network->send(std::move(packet)))
            return true;
        append(L"傳送佇列已滿或連線已關閉，請稍後重試。", RGB(255, 145, 145));
        return false;
    }
    void layout() {
        RECT rect;
        GetClientRect(window, &rect);
        width = static_cast<int>(rect.right / scale);
        height = static_cast<int>(rect.bottom / scale);
        chatW = std::max(490, width - 560);
        place(Nick, 40, 137, 140, 30);
        place(Host, 196, 137, 164, 30);
        place(Port, 376, 137, 70, 30);
        place(Connect, 462, 133, 128, 38);
        place(Downloads, chatX + chatW - 146, 133, 130, 38);
        place(Refresh, 156, 233, 64, 28);
        place(Rooms, 40, 277, 180, std::max(80, height - 503));
        place(Join, 40, height - 210, 86, 32);
        place(Leave, 134, height - 210, 86, 32);
        place(RoomName, 40, height - 139, 180, 30);
        place(Create, 40, height - 95, 180, 36);
        place(History, chatX + chatW - 106, 230, 90, 30);
        place(Transcript, chatX + 16, 278, chatW - 32, height - 434);
        place(Input, chatX + 16, height - 140, chatW - 32, 58);
        place(File, chatX + 16, height - 66, 104, 34);
        place(Send, chatX + chatW - 116, height - 66, 100, 34);
        InvalidateRect(window, nullptr, TRUE);
    }
    void paint() {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        RECT rect;
        GetClientRect(window, &rect);
        HDC memory = CreateCompatibleDC(dc);
        HBITMAP bitmap =
            CreateCompatibleBitmap(dc, std::max(1L, rect.right), std::max(1L, rect.bottom));
        HGDIOBJ previous = SelectObject(memory, bitmap);
        {
            Gdiplus::Graphics graphics(memory);
            graphics.Clear(Gdiplus::Color(16, 17, 24));
            if (background) {
                float factor = std::max(float(rect.right) / background->GetWidth(),
                                        float(rect.bottom) / background->GetHeight());
                float w = background->GetWidth() * factor, h = background->GetHeight() * factor;
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(background.get(), Gdiplus::RectF((rect.right - w) / 2,
                                                                    (rect.bottom - h) / 2, w, h));
            }
            Gdiplus::SolidBrush shade(Gdiplus::Color(70, 9, 10, 18));
            graphics.FillRectangle(&shade, 0, 0, rect.right, rect.bottom);
            Gdiplus::SolidBrush panel(Gdiplus::Color(236, 17, 18, 26));
            graphics.FillRectangle(&panel, px(24), px(92), px(chatX + chatW - 24), px(102));
            graphics.FillRectangle(&panel, px(24), px(212), px(212), px(height - 236));
            graphics.FillRectangle(&panel, px(chatX), px(212), px(chatW), px(height - 236));
            Gdiplus::SolidBrush red(Gdiplus::Color(228, 64, 89));
            graphics.FillRectangle(&red, px(24), px(24), px(4), px(42));
        }
        label(memory, L"夜航  /  NIGHTLINK", 40, 20, 600, 38, title, Ink);
        label(memory, L"留一盞燈，等一句訊息。", 42, 60, 500, 22, small, Muted);
        label(memory, L"暱稱", 40, 107, 140, 22, small, Muted);
        label(memory, L"伺服器 IPv4", 196, 107, 164, 22, small, Muted);
        label(memory, L"連接埠", 376, 107, 70, 22, small, Muted);
        label(memory, L"房間", 40, 231, 100, 32, font, Ink);
        label(memory, L"建立新房間", 40, height - 171, 180, 24, small, Muted);
        label(memory,
              L"#  " +
                  (connected ? (currentRoom.empty() ? L"尚未加入房間" : currentRoom) : L"等待連線"),
              chatX + 16, 230, chatW - 130, 32, font, Ink);
        label(memory, L"Enter 傳送 · Shift + Enter 換行", chatX + 130, height - 63, chatW - 250, 28,
              small, Muted);
        label(memory,
              connected ? L"●  ONLINE"
              : busy    ? L"●  CONNECTING"
                        : L"○  OFFLINE",
              chatX + chatW + 24, height - 155, 245, 28, font,
              connected ? RGB(138, 218, 185) : Ink);
        label(memory, status, chatX + chatW + 24, height - 121, 245, 26, small, Ink);
        label(memory, L"NIGHTLINK / 私人頻道", chatX + chatW + 24, height - 71, 245, 22, small,
              Muted);
        BitBlt(dc, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
        SelectObject(memory, previous);
        DeleteObject(bitmap);
        DeleteDC(memory);
        EndPaint(window, &ps);
    }
    void drawButton(DRAWITEMSTRUCT *item) {
        bool enabled = IsWindowEnabled(item->hwndItem) != FALSE;
        bool primary = item->CtlID == Connect || item->CtlID == Send;
        COLORREF color = !enabled ? RGB(32, 33, 42) : primary ? Accent : RGB(43, 44, 56);
        if (item->itemState & ODS_SELECTED)
            color = RGB(120, 36, 55);
        HBRUSH fill = CreateSolidBrush(color);
        FillRect(item->hDC, &item->rcItem, fill);
        DeleteObject(fill);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, enabled ? Ink : RGB(92, 93, 108));
        SelectObject(item->hDC, font);
        auto text = value(item->hwndItem);
        RECT rect = item->rcItem;
        DrawTextW(item->hDC, text.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (item->itemState & ODS_FOCUS) {
            InflateRect(&rect, -3, -3);
            DrawFocusRect(item->hDC, &rect);
        }
    }
    void connect() {
        // 同一按鈕依狀態負責連線、取消連線或斷線；網路工作交給 GuiConnection。
        if (busy || connected) {
            network->stop();
            busy = connected = false;
            currentRoom.clear();
            status = L"已中斷連線";
            SendMessageW(get(Rooms), LB_RESETCONTENT, 0, 0);
            append(L"已中斷連線。", Muted);
            updateControls();
            return;
        }
        nickname = value(get(Nick));
        auto name = utf8(nickname);
        if (name.empty() || name.size() > 32 ||
            name.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
                std::string::npos) {
            append(L"暱稱限 1–32 個英文字母、數字、_ 或 -。", RGB(255, 145, 145));
            SetFocus(get(Nick));
            return;
        }
        auto text = value(get(Port));
        if (text.empty() || text.size() > 5 ||
            text.find_first_not_of(L"0123456789") != std::wstring::npos || std::stoi(text) < 1 ||
            std::stoi(text) > 65535) {
            append(L"連接埠必須介於 1 到 65535。", RGB(255, 145, 145));
            return;
        }
        busy = true;
        status = L"正在連線…";
        updateControls();
        try {
            network->start(utf8(value(get(Host))), static_cast<unsigned short>(std::stoi(text)),
                           name);
        } catch (const std::exception &e) {
            busy = false;
            append(wide(e.what()), RGB(255, 145, 145));
            updateControls();
        }
    }
    void sendText() {
        // 寬字元輸入先轉 UTF-8，以 bytes 檢查大小，中文字不等於一 byte。
        if (!connected || currentRoom.empty())
            return;
        auto text = utf8(value(get(Input)));
        if (text.empty())
            return;
        if (text.size() > 4096) {
            append(L"訊息超過 4096 bytes，請縮短後再傳送。", RGB(255, 145, 145));
            return;
        }
        if (submit({PacketType::SendMessage, {text}})) {
            SetWindowTextW(get(Input), L"");
            SetFocus(get(Input));
        }
    }
    void sendFile() {
        // 只傳 basename 與檔案內容，不把使用者的完整本機路徑送給伺服器。
        wchar_t filename[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof dialog;
        dialog.hwndOwner = window;
        dialog.lpstrFilter = L"所有檔案\0*.*\0";
        dialog.lpstrFile = filename;
        dialog.nMaxFile = 32768;
        dialog.lpstrTitle = L"選擇要傳送的檔案（最大 1 MiB）";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog))
            return;
        try {
            std::filesystem::path path(filename);
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input || input.tellg() < 0 || input.tellg() > 1024 * 1024)
                throw std::runtime_error("File missing or larger than 1 MiB");
            std::string content(static_cast<size_t>(input.tellg()), '\0');
            input.seekg(0);
            if (!input.read(content.data(), static_cast<std::streamsize>(content.size())))
                throw std::runtime_error("Cannot read file");
            if (submit({PacketType::SendFile, {path.filename().u8string(), std::move(content)}}))
                append(L"正在傳送：" + path.filename().wstring(), Muted);
        } catch (const std::exception &e) {
            append(L"檔案傳送失敗：" + wide(e.what()), RGB(255, 145, 145));
        }
    }
    void command(int id, int notification) {
        // WM_COMMAND 的控制項 ID 決定操作；房間列表只在雙擊時直接加入。
        if (id == Connect) {
            connect();
            return;
        }
        if (id == Downloads) {
            try {
                std::filesystem::create_directories(downloads);
                ShellExecuteW(window, L"open", downloads.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } catch (const std::exception &e) {
                append(wide(e.what()), RGB(255, 145, 145));
            }
            return;
        }
        if (!connected)
            return;
        switch (id) {
        case Refresh:
            submit({PacketType::ListRooms, {}});
            break;
        case Rooms:
            if (notification != LBN_DBLCLK)
                break;
            [[fallthrough]];
        case Join: {
            auto selected = SendMessageW(get(Rooms), LB_GETCURSEL, 0, 0);
            if (selected == LB_ERR) {
                append(L"請先選擇左側的房間。", Muted);
                break;
            }
            auto length = SendMessageW(get(Rooms), LB_GETTEXTLEN, selected, 0);
            if (length == LB_ERR || length > 32) {
                append(L"無效的房間名稱。", Muted);
                break;
            }
            std::wstring room(static_cast<size_t>(length) + 1, L'\0');
            SendMessageW(get(Rooms), LB_GETTEXT, selected, reinterpret_cast<LPARAM>(room.data()));
            room.resize(static_cast<size_t>(length));
            submit({PacketType::JoinRoom, {utf8(room)}});
            break;
        }
        case Leave:
            submit({PacketType::LeaveRoom, {}});
            break;
        case Create: {
            auto room = utf8(value(get(RoomName)));
            if (room.empty()) {
                append(L"請輸入新房間名稱。", Muted);
                break;
            }
            submit({PacketType::CreateRoom, {room}});
            break;
        }
        case History:
            submit({PacketType::History, {}});
            break;
        case Send:
            sendText();
            break;
        case File:
            if (!currentRoom.empty())
                sendFile();
            break;
        }
    }
    void incoming() {
        // 由 UI 執行緒處理 NetworkEvent；背景收送執行緒不直接操作控制項。
        // File 的第四欄已由接收端轉成下載路徑，這裡用該路徑顯示附件及縮圖。
        for (auto &event : network->drain()) {
            if (event.kind == GuiEvent::Connected) {
                busy = false;
                connected = true;
                currentRoom = L"lobby";
                status = nickname + L" · lobby";
                append(L"已連線，歡迎 " + nickname + L"。已加入 lobby。", RGB(138, 218, 185));
                submit({PacketType::ListRooms, {}});
                SetFocus(get(Input));
            } else if (event.kind == GuiEvent::Failure) {
                append(L"提示：" + wide(event.text), RGB(255, 145, 145));
            } else if (event.kind == GuiEvent::Disconnected) {
                busy = connected = false;
                currentRoom.clear();
                status = L"連線已關閉 · 可重新連線";
                SendMessageW(get(Rooms), LB_RESETCONTENT, 0, 0);
                append(L"連線已關閉，請確認伺服器或重新連線。", Muted);
            } else {
                auto &p = event.packet;
                if (p.type == PacketType::Rooms && p.fields.size() == 1) {
                    SendMessageW(get(Rooms), LB_RESETCONTENT, 0, 0);
                    std::istringstream lines(p.fields[0]);
                    std::string room;
                    while (std::getline(lines, room)) {
                        auto name = wide(room);
                        SendMessageW(get(Rooms), LB_ADDSTRING, 0,
                                     reinterpret_cast<LPARAM>(name.c_str()));
                    }
                    SendMessageW(get(Rooms), LB_SELECTSTRING, static_cast<WPARAM>(-1),
                                 reinterpret_cast<LPARAM>(currentRoom.c_str()));
                } else if (p.type == PacketType::Message && p.fields.size() == 4) {
                    append(L"[" + wide(p.fields[0]) + L"]  " + wide(p.fields[1]) + L"\r\n" +
                               wide(p.fields[2]),
                           p.fields[1] == utf8(nickname) ? RGB(242, 169, 181) : Ink);
                } else if (p.type == PacketType::File && p.fields.size() == 4) {
                    append(L"[" + wide(p.fields[0]) + L"]  " + wide(p.fields[1]) + L" 傳送檔案：" +
                               wide(p.fields[2]) + L"\r\n已儲存：" + wide(p.fields[3]),
                           RGB(138, 218, 185));
                    if (!appendInlineImage(get(Transcript), std::filesystem::u8path(p.fields[3]),
                                           px(360), px(230), static_cast<int>(96 * scale)))
                        append(L"此檔案沒有圖片預覽，可從下載資料夾開啟。", Muted);
                } else if (!p.fields.empty()) {
                    const auto &text = p.fields[0];
                    if (p.type == PacketType::Ok && text.rfind("Joined ", 0) == 0) {
                        auto nextRoom = wide(text.substr(7));
                        if (nextRoom != currentRoom) {
                            SetWindowTextW(get(Transcript), L"");
                        }
                        currentRoom = nextRoom;
                        status = nickname + L" · " + currentRoom;
                    }
                    if (p.type == PacketType::Ok && text == "Left room") {
                        currentRoom.clear();
                        status = nickname + L" · 尚未加入房間";
                    }
                    if (p.type == PacketType::Ok && text.rfind("Room created", 0) == 0) {
                        submit({PacketType::ListRooms, {}});
                        SetWindowTextW(get(RoomName), L"");
                    }
                    std::wstring display = wide(text);
                    if (p.type == PacketType::Ok) {
                        if (text.rfind("Room created", 0) == 0)
                            display = L"房間已建立，選取後按「加入」或雙擊房間名稱。";
                        else if (text.rfind("Joined ", 0) == 0)
                            display = L"已加入 " + currentRoom;
                        else if (text == "Left room")
                            display = L"已離開房間。";
                        else if (text == "End of history")
                            display = L"歷史訊息讀取完畢。";
                    }
                    append((p.type == PacketType::Error ? L"錯誤：" : L"系統：") + display,
                           p.type == PacketType::Error ? RGB(255, 145, 145) : Muted);
                }
            }
        }
        updateControls();
    }
    void initialize();
};
LRESULT CALLBACK inputProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                DWORD_PTR data) {
    auto app = reinterpret_cast<App *>(data);
    if (message == WM_IME_STARTCOMPOSITION)
        app->composing = true;
    if (message == WM_IME_ENDCOMPOSITION)
        app->composing = false;
    if (message == WM_KEYDOWN && wParam == VK_RETURN && !app->composing &&
        !(GetKeyState(VK_SHIFT) & 0x8000)) {
        app->sendText();
        return 0;
    }
    if (message == WM_CHAR && wParam == L'\r' && !app->composing &&
        !(GetKeyState(VK_SHIFT) & 0x8000))
        return 0;
    return DefSubclassProc(hwnd, message, wParam, lParam);
}
void App::initialize() {
    HDC dc = GetDC(window);
    scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
    ReleaseDC(window, dc);
    font = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, 0, L"Microsoft JhengHei UI");
    small = CreateFontW(-px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                        CLEARTYPE_QUALITY, 0, L"Microsoft JhengHei UI");
    title = CreateFontW(-px(27), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                        CLEARTYPE_QUALITY, 0, L"Microsoft JhengHei UI");
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));
    if (resource) {
        DWORD size = SizeofResource(nullptr, resource);
        void *source = LockResource(LoadResource(nullptr, resource));
        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, size);
        if (data) {
            void *destination = GlobalLock(data);
            if (destination) {
                CopyMemory(destination, source, size);
                GlobalUnlock(data);
                if (SUCCEEDED(CreateStreamOnHGlobal(data, TRUE, &imageStream))) {
                    background.reset(Gdiplus::Bitmap::FromStream(imageStream));
                    if (background->GetLastStatus() != Gdiplus::Ok)
                        background.reset();
                } else
                    GlobalFree(data);
            } else
                GlobalFree(data);
        }
    }
    add(Nick, L"EDIT", L"", ES_AUTOHSCROLL);
    add(Host, L"EDIT", L"", ES_AUTOHSCROLL);
    SendMessageW(get(Host), EM_SETCUEBANNER, FALSE,
                 reinterpret_cast<LPARAM>(L"伺服器的 Tailscale 或區網 IPv4"));
    add(Port, L"EDIT", L"9000", ES_NUMBER | ES_AUTOHSCROLL);
    for (int id : {Nick, Host, Port}) {
        SendMessageW(get(id), EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(px(7), px(7)));
        SendMessageW(get(id), EM_SETLIMITTEXT, id == Port ? 5 : 32, 0);
    }
    add(Connect, L"BUTTON", L"連線聊天室", BS_OWNERDRAW);
    add(Downloads, L"BUTTON", L"下載資料夾", BS_OWNERDRAW);
    add(Rooms, L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT);
    add(Refresh, L"BUTTON", L"更新", BS_OWNERDRAW);
    add(Join, L"BUTTON", L"加入", BS_OWNERDRAW);
    add(Leave, L"BUTTON", L"離開", BS_OWNERDRAW);
    add(RoomName, L"EDIT", L"", ES_AUTOHSCROLL);
    SendMessageW(get(RoomName), EM_SETLIMITTEXT, 32, 0);
    add(Create, L"BUTTON", L"＋ 建立房間", BS_OWNERDRAW);
    add(Transcript, MSFTEDIT_CLASS, L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL);
    SendMessageW(get(Transcript), EM_SETBKGNDCOLOR, 0, Surface);
    SendMessageW(get(Transcript), EM_EXLIMITTEXT, 0, 150000);
    add(Input, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL);
    SendMessageW(get(Input), EM_SETLIMITTEXT, 4096, 0);
    SendMessageW(get(Input), EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(px(8), px(8)));
    SetWindowSubclass(get(Input), inputProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    add(Send, L"BUTTON", L"傳送  →", BS_OWNERDRAW);
    add(File, L"BUTTON", L"＋ 傳送檔案", BS_OWNERDRAW);
    add(History, L"BUTTON", L"歷史訊息", BS_OWNERDRAW);
    network = std::make_unique<GuiConnection>(window, downloads);
    append(L"歡迎來到夜航。\r\n輸入暱稱，連上你的聊天室。", Muted);
    if (!background)
        append(L"背景圖片載入失敗。", RGB(255, 145, 145));
    layout();
    updateControls();
    SetFocus(get(Nick));
}
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto app = reinterpret_cast<App *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = reinterpret_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE:
        try {
            app->initialize();
        } catch (const std::exception &e) {
            MessageBoxW(window, wide(e.what()).c_str(), L"啟動失敗", MB_ICONERROR);
            return -1;
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO *>(lParam);
        info->ptMinTrackSize = {app->px(1080), app->px(730)};
        return 0;
    }
    case WM_SIZE:
        if (app->network && wParam != SIZE_MINIMIZED)
            app->layout();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        app->paint();
        return 0;
    case WM_DRAWITEM:
        app->drawButton(reinterpret_cast<DRAWITEMSTRUCT *>(lParam));
        return TRUE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), Ink);
        SetBkColor(reinterpret_cast<HDC>(wParam), Surface);
        return reinterpret_cast<LRESULT>(app->brush);
    case WM_COMMAND:
        app->command(LOWORD(wParam), HIWORD(wParam));
        return 0;
    case NetworkEvent:
        app->incoming();
        return 0;
    case WM_CLOSE:
        if (app->network)
            app->network->stop();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    SetProcessDPIAware();
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return 1;
    if (FAILED(OleInitialize(nullptr))) {
        WSACleanup();
        return 1;
    }
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
        OleUninitialize();
        WSACleanup();
        return 1;
    }
    HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");
    INITCOMMONCONTROLSEX common{sizeof common, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&common);
    int result = 1;
    {
        App app;
        WNDCLASSW klass{};
        klass.lpfnWndProc = windowProcedure;
        klass.hInstance = instance;
        klass.lpszClassName = L"NightlinkChat";
        klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        klass.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
        RegisterClassW(&klass);
        HDC dc = GetDC(nullptr);
        float scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
        ReleaseDC(nullptr, dc);
        HWND window = CreateWindowExW(
            WS_EX_CONTROLPARENT, klass.lpszClassName, L"夜航 NIGHTLINK — Winsock 聊天室",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
            static_cast<int>(1240 * scale), static_cast<int>(820 * scale), nullptr, nullptr,
            instance, &app);
        if (window) {
            ShowWindow(window, show);
            UpdateWindow(window);
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                // Multiline input handles Enter itself (including IME input); Tab uses dialog
                // navigation.
                if (message.hwnd == app.get(Input) && message.message == WM_KEYDOWN &&
                    message.wParam == VK_RETURN) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                } else if (!IsDialogMessageW(window, &message)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            result = 0;
        }
    }
    if (richEdit)
        FreeLibrary(richEdit);
    Gdiplus::GdiplusShutdown(token);
    OleUninitialize();
    WSACleanup();
    return result;
}
