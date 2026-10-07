// 使用 GDI+ 解碼縮圖，再透過 RTF/OLE 插入 RichEdit 聊天紀錄。
#include "InlineImage.h"
#include <objidl.h>
#include <gdiplus.h>
#include <richedit.h>
#include <richole.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <string>

namespace chat {
namespace {
// RichEdit needs client-owned storage for pictures imported from RTF.
class PictureStorage final : public IRichEditOleCallback {
    ULONG references_ = 1;

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IRichEditOleCallback)) {
            *object = static_cast<IRichEditOleCallback *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto count = --references_;
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE GetNewStorage(IStorage **storage) override {
        if (!storage)
            return E_POINTER;
        *storage = nullptr;
        ILockBytes *bytes = nullptr;
        auto result = CreateILockBytesOnHGlobal(nullptr, TRUE, &bytes);
        if (SUCCEEDED(result)) {
            result = StgCreateDocfileOnILockBytes(
                bytes, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, storage);
            bytes->Release();
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetInPlaceContext(IOleInPlaceFrame **, IOleInPlaceUIWindow **,
                                                OLEINPLACEFRAMEINFO *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE ShowContainerUI(BOOL) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInsertObject(CLSID *, IStorage *, LONG) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DeleteObject(IOleObject *) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryAcceptData(IDataObject *, CLIPFORMAT *, DWORD, BOOL,
                                              HGLOBAL) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetClipboardData(CHARRANGE *, DWORD, IDataObject **) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetDragDropEffect(BOOL, DWORD, DWORD *effect) override {
        if (effect)
            *effect = DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetContextMenu(WORD, IOleObject *, CHARRANGE *,
                                             HMENU *menu) override {
        if (menu)
            *menu = nullptr;
        return E_NOTIMPL;
    }
};
struct Stream {
    const std::string &bytes;
    size_t offset = 0;
};
DWORD CALLBACK readRtf(DWORD_PTR cookie, LPBYTE target, LONG length, LONG *read) {
    auto &stream = *reinterpret_cast<Stream *>(cookie);
    size_t count = std::min(static_cast<size_t>(length), stream.bytes.size() - stream.offset);
    std::memcpy(target, stream.bytes.data() + stream.offset, count);
    stream.offset += count;
    *read = static_cast<LONG>(count);
    return 0;
}
void boundImages(HWND transcript) {
    // 只限制嵌入圖片數量；超過 24 張時刪除最早的圖片物件。
    IRichEditOle *objects = nullptr;
    if (!SendMessageW(transcript, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&objects)) ||
        !objects)
        return;
    while (objects->GetObjectCount() > 24) {
        REOBJECT object{};
        object.cbStruct = sizeof object;
        if (FAILED(objects->GetObject(0, &object, REO_GETOBJ_NO_INTERFACES)))
            break;
        CHARRANGE range{object.cp, object.cp + 1};
        SendMessageW(transcript, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        SendMessageW(transcript, EM_REPLACESEL, FALSE,
                     reinterpret_cast<LPARAM>(L"[較早的圖片預覽已釋放；原檔仍保留]"));
    }
    objects->Release();
}
} // namespace
bool appendInlineImage(HWND transcript, const std::filesystem::path &path, int maxWidth,
                       int maxHeight, int dpi) {
    if (maxWidth < 1 || maxHeight < 1 || dpi < 1)
        return false;
    Gdiplus::Bitmap original(path.c_str());
    if (original.GetLastStatus() != Gdiplus::Ok)
        return false;
    GUID format{};
    if (original.GetRawFormat(&format) != Gdiplus::Ok ||
        !(IsEqualGUID(format, Gdiplus::ImageFormatPNG) ||
          IsEqualGUID(format, Gdiplus::ImageFormatJPEG) ||
          IsEqualGUID(format, Gdiplus::ImageFormatGIF) ||
          IsEqualGUID(format, Gdiplus::ImageFormatBMP)))
        return false;
    const auto originalWidth = original.GetWidth(), originalHeight = original.GetHeight();
    // 限制解碼後的尺寸及總像素，不只檢查附件檔案大小。
    if (!originalWidth || !originalHeight || originalWidth > 8192 || originalHeight > 8192 ||
        static_cast<uint64_t>(originalWidth) * originalHeight > 25000000)
        return false;
    // 保持比例且不放大原圖，同時符合寬高限制。
    double ratio = std::min({1.0, static_cast<double>(maxWidth) / originalWidth,
                             static_cast<double>(maxHeight) / originalHeight});
    int width = std::max(1, static_cast<int>(originalWidth * ratio));
    int height = std::max(1, static_cast<int>(originalHeight * ratio));
    Gdiplus::Bitmap thumbnail(width, height, PixelFormat24bppRGB);
    {
        Gdiplus::Graphics graphics(&thumbnail);
        graphics.Clear(Gdiplus::Color(24, 25, 34));
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        if (graphics.DrawImage(&original, 0, 0, width, height) != Gdiplus::Ok)
            return false;
    }
    HBITMAP bitmap = nullptr;
    if (thumbnail.GetHBITMAP(Gdiplus::Color(24, 25, 34), &bitmap) != Gdiplus::Ok)
        return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    info.bmiHeader.biCompression = BI_RGB;
    const size_t stride = (static_cast<size_t>(width) * 3 + 3) & ~size_t(3);
    // 24-bit DIB 每列需對齊 4 bytes，不能直接用 width * 3 當列長。
    const size_t pixelBytes = stride * height;
    std::vector<unsigned char> dib(sizeof(BITMAPINFOHEADER) + pixelBytes);
    HDC dc = GetDC(nullptr);
    int lines = GetDIBits(dc, bitmap, 0, height, dib.data() + sizeof(BITMAPINFOHEADER), &info,
                          DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    DeleteObject(bitmap);
    if (lines != height)
        return false;
    std::memcpy(dib.data(), &info.bmiHeader, sizeof(BITMAPINFOHEADER));
    std::string rtf = "{\\rtf1\\ansi{\\pict\\dibitmap0\\picw" + std::to_string(width) + "\\pich" +
                      std::to_string(height) + "\\picwgoal" + std::to_string(width * 1440 / dpi) +
                      "\\pichgoal" + std::to_string(height * 1440 / dpi) + " ";
    static constexpr char hex[] = "0123456789abcdef";
    // RTF 圖片資料以十六進位文字表示，每個 DIB byte 轉成兩個字元。
    rtf.reserve(rtf.size() + dib.size() * 2 + 32);
    for (auto byte : dib) {
        rtf.push_back(hex[byte >> 4]);
        rtf.push_back(hex[byte & 15]);
    }
    rtf += "}\\par}";
    auto callback = new PictureStorage;
    auto attached =
        SendMessageW(transcript, EM_SETOLECALLBACK, 0, reinterpret_cast<LPARAM>(callback));
    callback->Release();
    if (!attached)
        return false;
    IRichEditOle *objects = nullptr;
    if (!SendMessageW(transcript, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&objects)) ||
        !objects)
        return false;
    auto before = objects->GetObjectCount();
    Stream source{rtf};
    EDITSTREAM stream{};
    stream.dwCookie = reinterpret_cast<DWORD_PTR>(&source);
    stream.pfnCallback = readRtf;
    SendMessageW(transcript, EM_SETSEL, static_cast<WPARAM>(-1), -1);
    SendMessageW(transcript, EM_STREAMIN, SF_RTF | SFF_SELECTION,
                 reinterpret_cast<LPARAM>(&stream));
    auto after = objects->GetObjectCount();
    objects->Release();
    if (stream.dwError || after != before + 1)
        return false;
    boundImages(transcript);
    SendMessageW(transcript, EM_SETSEL, static_cast<WPARAM>(-1), -1);
    SendMessageW(transcript, EM_SCROLLCARET, 0, 0);
    return true;
}
} // namespace chat
