#pragma once

#include <string>
#include <string_view>

namespace amberedit::encoding {

/// A recoder on top of POSIX iconv. The iconv_t descriptor lives inside and
/// is closed in the destructor — no C resources leak out.
///
/// The object is meant to be reused: convert() remembers the last charset
/// pair instead of reopening a descriptor for every message.
class IconvRecoder {
public:
    IconvRecoder() = default;
    ~IconvRecoder();

    IconvRecoder(const IconvRecoder&) = delete;
    IconvRecoder& operator=(const IconvRecoder&) = delete;
    IconvRecoder(IconvRecoder&& other) noexcept;
    IconvRecoder& operator=(IconvRecoder&& other) noexcept;

    /// Converts text from `fromCharset` to UTF-8.
    ///
    /// This never throws: broken bytes become U+FFFD, and an unknown charset
    /// means the input is returned unchanged. Reading 30-year-old mail matters
    /// more than being strict about its encoding.
    std::string toUtf8(std::string_view text, const std::string& fromCharset);

    /// Converts UTF-8 text into `toCharset`, for a message on its way into a
    /// base. Like toUtf8 it never throws: a character the target charset has no
    /// room for becomes '?' rather than costing the message, and an unknown
    /// charset means the UTF-8 goes out unchanged.
    std::string fromUtf8(std::string_view text, const std::string& toCharset);

    /// The last error (e.g. "unknown charset X"); empty if there was none.
    [[nodiscard]] const std::string& lastError() const { return lastError_; }

private:
    void closeDescriptor();
    bool ensureDescriptor(const std::string& fromCharset);
    void closeOutDescriptor();
    bool ensureOutDescriptor(const std::string& toCharset);

    void* descriptor_{nullptr};  ///< iconv_t, kept as void* to avoid iconv.h here
    std::string currentFrom_;
    /// The other direction has a converter of its own: iconv descriptors are
    /// one-way, and a reader that has just started writing should not have to
    /// reopen one per line.
    void* outDescriptor_{nullptr};
    std::string currentTo_;
    std::string lastError_;
};

/// True if the string is well-formed UTF-8. Used both to avoid recoding text
/// that already is UTF-8 and to keep broken bytes out of the terminal.
bool isValidUtf8(std::string_view text);

}  // namespace amberedit::encoding
