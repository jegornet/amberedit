#pragma once

#include <string>
#include <string_view>

#include "support/result.hpp"

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

    /// Converts text from `fromCharset` to UTF-8, or says why it could not — a
    /// charset iconv does not know being the only thing that can go wrong.
    ///
    /// Broken bytes are not a failure either way: they become U+FFFD. This is
    /// the form for a caller that must not accept an approximation — an export,
    /// an import — where the charset was typed a moment ago and a mistyped one
    /// would go to disk as mojibake nobody could undo.
    [[nodiscard]] Result<std::string> intoUtf8(std::string_view text,
                                               const std::string& fromCharset);

    /// The same the other way, for a message on its way into a base.
    [[nodiscard]] Result<std::string> intoCharset(std::string_view text,
                                                  const std::string& toCharset);

    /// The same two conversions for a reader, where an approximation is the
    /// right answer: an unknown charset hands the text back as it stands rather
    /// than costing the message, and a character the target charset has no room
    /// for becomes '?'. This is what every message read out of a base goes
    /// through — reading 30-year-old mail matters more than being strict about
    /// its encoding — and the two above are what the export and the import use.
    std::string toUtf8(std::string_view text, const std::string& fromCharset) {
        return intoUtf8(text, fromCharset).value_or(std::string(text));
    }

    std::string fromUtf8(std::string_view text, const std::string& toCharset) {
        return intoCharset(text, toCharset).value_or(std::string(text));
    }

private:
    void closeDescriptor();
    [[nodiscard]] Result<void> ensureDescriptor(const std::string& fromCharset);
    void closeOutDescriptor();
    [[nodiscard]] Result<void> ensureOutDescriptor(const std::string& toCharset);

    void* descriptor_{nullptr};  ///< iconv_t, kept as void* to avoid iconv.h here
    std::string currentFrom_;
    /// The other direction has a converter of its own: iconv descriptors are
    /// one-way, and a reader that has just started writing should not have to
    /// reopen one per line.
    void* outDescriptor_{nullptr};
    std::string currentTo_;
};

/// Whether iconv on this machine knows the charset, both ways round — a name it
/// will decode from but will not encode into is no use to a config that states
/// it as the charset messages are written in. Nothing is converted: this opens
/// two descriptors and closes them again.
///
/// It is what asks the question the config layer never asks. A charset there is
/// a word taken as it is written and only tried at the first message, which is
/// right for a config somebody keeps — but the setup wizard has the user in
/// front of it, and a typo is worth catching while there is somebody to fix it.
[[nodiscard]] Result<void> checkCharset(const std::string& charset);

/// True if the string is well-formed UTF-8. Used both to avoid recoding text
/// that already is UTF-8 and to keep broken bytes out of the terminal.
bool isValidUtf8(std::string_view text);

}  // namespace amberedit::encoding
