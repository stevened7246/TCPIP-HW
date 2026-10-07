# Winsock 多執行緒聊天室

透過 Tailscale 分享給朋友使用，請先閱讀 [使用說明書](說明書.md)，內含主機設定、客戶端分發、三項功能驗收與故障排除。打包指令為 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1`（先完成建置與測試）。

Windows / C++17，使用原生 Winsock TCP，無第三方 C++ 套件依賴。

## 編譯

需要 CMake 3.16 以上，以及 MinGW-w64 或 Visual Studio C++ 工具鏈。本機使用 MSYS2 UCRT64，在 PowerShell 執行：

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j 4
```

Visual Studio 使用另一個建置目錄：

```powershell
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Release
```

MinGW 產物需要對應工具鏈的 DLL 位於 PATH，本機編譯環境已具備。Visual Studio 產物位於 `build-vs/Release/`。

## 啟動

第一個 PowerShell 視窗：

```powershell
.\build\chat_server.exe
```

另開兩個視窗，各執行一個用戶端：

```powershell
.\build\chat_client.exe Alice
.\build\chat_client.exe Bob
```

登入後自動加入 `lobby`，直接輸入文字即可互相聊天。暱稱在連線期間不可重複，斷線後會釋放。

自訂連接埠與監聽位址：

```powershell
.\build\chat_server.exe 9000 0.0.0.0
.\build\chat_client.exe Alice 192.168.1.10 9000
```

預設監聽 `0.0.0.0:9000`，接受所有本機 IPv4 介面上的連線。區域網路使用時，換成伺服器實際 IP，並自行允許 Windows 防火牆的對應 TCP 連接埠。伺服器輸入 `/quit` 或關閉標準輸入，可中斷 socket 並等待工作執行緒結束。

## 圖形介面（夜航 NIGHTLINK）

完成編譯後，啟動伺服器，再開啟圖形用戶端：

```powershell
.\build\chat_server.exe
.\build\gui\chat_gui.exe
```

若伺服器已在執行，只需開啟 `chat_gui.exe`。可同時開多個 GUI 視窗，也可與原本命令列用戶端互通。Visual Studio 的 GUI 路徑為 `build-vs/gui/Release/chat_gui.exe`。

- 輸入暱稱、伺服器 IPv4 與連接埠，按「連線聊天室」。伺服器欄位預設留空，請填主機的 Tailscale 或區網 IPv4；連接埠預設 9000。不要填 `0.0.0.0`。
- 房間列表可雙擊加入，或選取後按「加入」。下方可建立新房間；其他用戶建立房間後，按「更新」取得清單。
- 文字輸入區按 Enter 傳送，Shift + Enter 換行；也可按「傳送」。中文輸入法組字期間不會觸發 Enter 傳送。
- 「歷史訊息」讀取目前房間紀錄。「傳送檔案」開啟 Windows 檔案選擇器，限制 1 MiB。
- GUI 接收的檔案存於 **GUI 執行檔旁**的 `downloads/`，按「下載資料夾」可開啟。檔案以 `received_<PID>_<序號>.<原副檔名>` 儲存，原始名稱顯示於訊息中；沒有一般副檔名時使用 `.bin`。不會自動執行檔案。
- 「中斷連線」後可重新登入；連線期間也可按「取消連線」。

背景直接使用 `asset/聊天室背景.png`，透過 Windows resource 嵌入執行檔。Win32 / GDI+ 繪製黑紅色背景與面板，RichEdit 顯示聊天內容。背景隨視窗等比例填滿，左右可能裁切；支援視窗縮放、系統 DPI、Tab 鍵導覽。

新增的 `GuiMain.cpp` 負責視窗與互動，`GuiConnection.h/.cpp` 負責背景連線、接收／傳送執行緒及有上限的訊息佇列。worker 以 `WM_APP` 通知 UI，由主執行緒更新控制項。連線最長等待約 5 秒，網路錯誤顯示於聊天區。GUI 不提供網頁介面，也未改變伺服器驗證與儲存方式。

## 用戶端指令

| 指令 | 功能 |
| --- | --- |
| 一般文字 | 傳送訊息給目前房間全部成員，含自己 |
| `/rooms` | 列出房間 |
| `/create study` | 建立房間，不自動切換 |
| `/join study` | 切換至指定房間 |
| `/leave` | 離開目前房間 |
| `/history` | 讀取目前房間最近 100 則文字訊息 |
| `/file D:\example\image.png` | 傳送檔案，路徑可含空格，不需引號 |
| `/quit` | 關閉用戶端 |

暱稱與房間名稱限 1–32 個 ASCII 英文字母、數字、底線或連字號。訊息支援 UTF-8 中文，單則最多 4096 bytes。檔案最多 1 MiB，檔名最多 128 bytes；禁止路徑分隔符號等不安全字元。

收到的檔案（包含自己送出的副本）存於用戶端**目前工作目錄**下的 `downloads/received_<PID>_<序號>.<原副檔名>`。畫面顯示原始名稱與儲存位置，一般副檔名會保留；無副檔名或不適用時以 `.bin` 儲存。產生的名稱避免遠端檔名指定本機路徑，並跳過已存在的檔案。

## 分層

```text
main.cpp                       Winsock 初始化、啟動與停止
API/                           依功能將請求交給 service
  AuthHandler.cpp
  RoomHandler.cpp
  MessageHandler.cpp
  FileHandler.cpp
domain/                        User、Room、Message、ClientSession
include/
  protocol/                    PacketType、PacketHeader、Packet 宣告
  ChatServices.h               業務服務與 handler 宣告
  Repositories.h               記憶體 repository 宣告
  Server.h                     伺服器宣告
service/
  Server.cpp                   accept、分派封包、工作執行緒生命週期
  AuthService.cpp              暱稱登入
  RoomService.cpp              房間建立、查詢、切換
  RoomSessionService.cpp       線上成員、廣播、斷線清理
  MessageService.cpp           訊息與歷史
  FileService.cpp              同房檔案轉送
  protocol/
    PacketParser.cpp           接收與驗證封包
    PacketSerializer.cpp       序列化與完整傳送
repository/                    User、Room、Message 記憶體資料存取
public/userinterface/
  main.cpp                     C++ 命令列用戶端
  GuiMain.cpp                  Windows 圖形介面
  GuiConnection.h/.cpp         GUI 背景網路連線
tests/integration.py           真實 TCP 與原生用戶端整合測試
```

`include/protocol/packet_type.h` 保留為相容 include，實際列舉在 `PacketType.h`。

伺服器每連線建立一條 worker，最多 64 條；已結束 worker 會定期 join 並回收。用戶端背景執行緒負責收訊，主執行緒負責鍵盤輸入。共享業務狀態使用同一把 mutex 序列化；每個 session 另有傳送 mutex，避免同一 socket 的封包交錯。repository 只能在業務 mutex 保護下存取。

廣播目前也在業務 mutex 內執行，以維持一致的房間事件順序；慢速接收者可能延遲其他請求。傳送逾時為 3 秒，接收等待逾時為 5 分鐘。這是學習／小型聊天室實作，大量連線可改為獨立傳送佇列及 IOCP。

## 封包協定

固定 12-byte header，不直接傳送 C++ struct；整數採 network byte order（big endian）。

| offset | 大小 | 欄位 |
| --- | --- | --- |
| 0 | 4 | magic：`0x43484154`（CHAT） |
| 4 | 2 | version：1 |
| 6 | 2 | PacketType |
| 8 | 4 | payload bytes |

Payload 為零到 16 個欄位，每欄位是 `uint32 長度 + 原始 bytes`，沒有分隔符號或結尾 NUL。文字採 UTF-8，檔案可含任意 byte。最大 payload 為 1 MiB + 4096 bytes。接收端完整讀取 header/body，處理 TCP 拆包及黏包；長度、magic 或版本錯誤會中斷連線。

| Type | 請求或回應欄位 |
| --- | --- |
| 1 Login | 暱稱 |
| 2 ListRooms | 無 |
| 3 CreateRoom / 4 JoinRoom | 房間名稱 |
| 5 LeaveRoom | 無 |
| 6 SendMessage | 訊息文字 |
| 7 History | 無 |
| 8 SendFile | 原始檔名、檔案 bytes |
| 100 Ok / 101 Error | 說明文字 |
| 102 Rooms | 以換行分隔的房間清單 |
| 103 Message | 房間、發送者、文字、Unix 秒數 |
| 104 File | 房間、發送者、原始檔名、檔案 bytes |

歷史查詢依序回傳 Message 封包，最後以 Ok 表示結束。新訊息與檔案事件傳給同房所有成員。未知 type 或業務驗證失敗回傳 Error。

## 測試

安裝 Python 3 後重新 configure，CMake 會自動註冊整合測試：

```powershell
ctest --test-dir build --output-on-failure
# Visual Studio：ctest --test-dir build-vs -C Release --output-on-failure
```

也可直接執行：

```powershell
python tests/integration.py build/chat_server.exe build/chat_client.exe
```

測試選擇臨時本機連接埠，啟動真實伺服器與用戶端，驗證登入／重複名稱、斷線清理、房間隔離、中文與歷史、TCP 拆包／黏包、1 MiB 二進位檔案、非法封包、8 個連線並行廣播、原生用戶端指令及檔案儲存、port 衝突與半包連線的正常關機。

## 實作範圍

- 登入是暱稱登記，沒有密碼、帳號持久化或身份驗證；TCP 流量未加密，適合本機／可信任區網示範。
- 房間最多 100 個（含 lobby），沒有刪除功能。每房最多保留 100 則訊息，重啟後資料消失。
- 檔案僅即時轉送給目前同房成員，不提供伺服器儲存、離線下載或續傳。
- `public/userinterface/` 包含 Windows 圖形介面與命令列介面。瀏覽器仍需 WebSocket 或 HTTP gateway。
- 遠端斷線時，用戶端提示按 Enter 結束，因為主執行緒可能正在等待鍵盤輸入。

GUI 驗證：已透過真實視窗完成登入、中文雙向聊天、建立與加入房間，以及開啟／取消檔案選擇器。CTest 另包含 GUI 網路層的訊息、1 MiB 檔案收發及磁碟內容比對、重複暱稱拒絕、斷線、重新連線與無效位址檢查。自動化尚未完成原生檔案選擇器的完整送出操作；輸入法組字保護已實作，但未人工測試各種輸入法。

## 聊天區圖片預覽

GUI 收到檔案後，會依實際內容辨識 PNG、JPG/JPEG、BMP、GIF，將等比例縮圖插入聊天紀錄（GIF 為第一張畫面，不播放動畫）。圖片原檔與檔名、儲存位置仍保留。傳送者也會收到自己的圖片回傳並顯示。

副檔名不再一律變成 `.bin`：一般的原副檔名會保留並轉成小寫，同名檔案使用不同流水號，內容不會轉檔。沒有副檔名或副檔名不適用時仍使用 `.bin`。舊的 `.bin` 不會自動改名，但重新傳送時仍可按圖片內容辨識並顯示。

縮圖最大約 360 × 230 個介面像素；只預覽單邊不超過 8192 pixels、總像素不超過 2500 萬的圖片。聊天紀錄最多同時保留 24 張縮圖；較早的縮圖會替換成提示文字，下載原檔不刪除。PDF、影片、壓縮檔、損壞圖片等只顯示下載資訊。檔案傳輸上限仍是 1 MiB。

`InlineImage.cpp` 用 GDI+ 解碼縮圖，再經由 RichEdit RTF 與圖片儲存 callback 插入，不會將遠端檔案當作 RTF 或可執行物件執行。實作參考：[Microsoft EM_STREAMIN](https://learn.microsoft.com/en-us/windows/win32/controls/em-streamin)、[IRichEditOleCallback](https://learn.microsoft.com/en-us/windows/win32/api/richole/nn-richole-iricheditolecallback)。

測試新增 `inline_image_preview`，驗證四種格式實際插入圖片物件、以內容辨識 `.bin`、損壞／過大圖片拒絕、24 張縮圖上限及原聊天文字保留。GUI 傳輸測試亦驗證副檔名保留與檔案內容一致。
