// 測試圖片解碼、縮圖插入、格式辨識與 RichEdit 圖片數量限制。
#include "public/userinterface/InlineImage.h"
#include <objidl.h>
#include <gdiplus.h>
#include <richedit.h>
#include <richole.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
void require(bool condition, const char *text) {
    if (!condition)
        throw std::runtime_error(text);
}
CLSID encoder(const wchar_t *mime) {
    UINT count = 0, bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    std::vector<BYTE> storage(bytes);
    auto codecs = reinterpret_cast<Gdiplus::ImageCodecInfo *>(storage.data());
    Gdiplus::GetImageEncoders(count, bytes, codecs);
    for (UINT i = 0; i < count; ++i)
        if (wcscmp(codecs[i].MimeType, mime) == 0)
            return codecs[i].Clsid;
    throw std::runtime_error("image encoder missing");
}
LONG objectCount(HWND edit) {
    // 透過 OLE 物件數確認圖片真的插入 RichEdit，不只檢查函式回傳值。
    IRichEditOle *objects = nullptr;
    require(SendMessageW(edit, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&objects)) &&
                objects,
            "OLE interface missing");
    auto count = objects->GetObjectCount();
    objects->Release();
    return count;
}
int main() {
    // 測試自行初始化 OLE/GDI+ 與 RichEdit，直接檢查圖片物件及插入結果。
    require(SUCCEEDED(OleInitialize(nullptr)), "OLE startup");
    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR token = 0;
    require(Gdiplus::GdiplusStartup(&token, &startup, nullptr) == Gdiplus::Ok, "GDI startup");
    HMODULE module = LoadLibraryW(L"Msftedit.dll");
    HWND edit =
        CreateWindowExW(0, MSFTEDIT_CLASS, L"preserved text", WS_POPUP | ES_MULTILINE | ES_READONLY,
                        0, 0, 600, 600, nullptr, nullptr, nullptr, nullptr);
    int result = 0;
    auto dir = std::filesystem::temp_directory_path() /
               (L"nightlink_preview_" + std::to_wstring(GetCurrentProcessId()));
    bool ownsDirectory = false;
    try {
        require(edit != nullptr, "rich edit startup");
        ownsDirectory = std::filesystem::create_directory(dir);
        require(ownsDirectory, "temporary fixture directory already exists");
        Gdiplus::Bitmap original(120, 80, PixelFormat24bppRGB);
        {
            Gdiplus::Graphics g(&original);
            g.Clear(Gdiplus::Color(220, 40, 90));
            Gdiplus::SolidBrush green(Gdiplus::Color(0, 220, 90));
            g.FillRectangle(&green, 10, 10, 40, 30);
        }
        struct Format {
            const wchar_t *mime;
            const wchar_t *name;
        };
        LONG count = 0;
        for (auto format :
             {Format{L"image/png", L"sample.png"}, Format{L"image/jpeg", L"sample.jpg"},
              Format{L"image/bmp", L"sample.bmp"}, Format{L"image/gif", L"sample.gif"}}) {
            auto clsid = encoder(format.mime);
            auto path = dir / format.name;
            require(original.Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok, "save fixture");
            require(chat::appendInlineImage(edit, path, 360, 230, 96), "insert picture");
            require(objectCount(edit) == ++count, "picture not inserted as inline object");
        }
        auto unknown = dir / L"sample.bin";
        std::filesystem::copy_file(dir / L"sample.png", unknown);
        require(chat::appendInlineImage(edit, unknown, 360, 230, 96), "image content detection");
        require(objectCount(edit) == ++count, "bin image object missing");
        auto broken = dir / L"broken.png";
        {
            std::ofstream file(broken);
            file << "not an image";
        }
        require(!chat::appendInlineImage(edit, broken, 360, 230, 96), "corrupt image accepted");
        require(objectCount(edit) == count, "corrupt image changed transcript");
        Gdiplus::Bitmap oversized(8193, 1, PixelFormat24bppRGB);
        auto png = encoder(L"image/png");
        require(oversized.Save((dir / L"oversized.png").c_str(), &png, nullptr) == Gdiplus::Ok,
                "oversized fixture");
        require(!chat::appendInlineImage(edit, dir / L"oversized.png", 360, 230, 96),
                "oversized image accepted");
        for (int i = 0; i < 25; ++i)
            require(chat::appendInlineImage(edit, dir / L"sample.png", 360, 230, 96),
                    "retention insert");
        require(objectCount(edit) == 24, "preview retention limit");
        std::vector<wchar_t> text(GetWindowTextLengthW(edit) + 1);
        GetWindowTextW(edit, text.data(), static_cast<int>(text.size()));
        require(std::wstring(text.data()).find(L"preserved text") != std::wstring::npos,
                "previous text lost");
        std::cout << "Inline previews: PNG/JPG/BMP/GIF, content detection, corrupt/oversized "
                     "rejection, retention passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    DestroyWindow(edit);
    FreeLibrary(module);
    // Only this test's freshly created, PID-scoped temporary fixtures are removed.
    if (ownsDirectory) {
        for (auto name : {L"sample.png", L"sample.jpg", L"sample.bmp", L"sample.gif", L"sample.bin",
                          L"broken.png", L"oversized.png"})
            std::filesystem::remove(dir / name);
        std::filesystem::remove(dir);
    }
    Gdiplus::GdiplusShutdown(token);
    OleUninitialize();
    return result;
}
