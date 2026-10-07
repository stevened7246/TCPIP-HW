// 從附件名稱取出可用副檔名，供用戶端產生自己的下載檔名。
#pragma once
#include <string>

namespace chat {
// Keep only an ordinary extension; never use a remote basename as a local path.
inline std::string attachmentExtension(const std::string &name) {
    // 只保留最後一段副檔名：image.PNG → .png，archive.tar.gz → .gz。
    // 副檔名須為 1–16 個 ASCII 英數字；缺少或不合法時改用 .bin。
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || name.size() - dot > 17 || dot + 1 == name.size())
        return ".bin";
    auto extension = name.substr(dot);
    for (size_t i = 1; i < extension.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(extension[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return ".bin";
        if (c >= 'A' && c <= 'Z')
            extension[i] = static_cast<char>(c - 'A' + 'a');
    }
    return extension;
}
} // namespace chat
