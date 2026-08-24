#include "encoding/iconv_recoder.hpp"

#include <iconv.h>

#include <cerrno>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace amberedit::encoding {
namespace {

iconv_t invalidDescriptor() {
    return reinterpret_cast<iconv_t>(-1);
}

iconv_t asIconv(void* p) {
    return reinterpret_cast<iconv_t>(p);
}

constexpr const char kReplacement[] = "\xEF\xBF\xBD";  // U+FFFD

}  // namespace

tl::expected<void, ErrorPtr> checkCharset(const std::string& charset) {
    if (charset.empty()) return failure("no charset is named");

    // Both directions, because both are what a config asks of a charset: a
    // message is read in one and written in the other.
    const iconv_t in = iconv_open("UTF-8//TRANSLIT", charset.c_str());
    if (in == invalidDescriptor()) {
        return failure("iconv does not know the charset '" + charset + "'");
    }
    iconv_close(in);

    const iconv_t out = iconv_open((charset + "//TRANSLIT").c_str(), "UTF-8");
    if (out == invalidDescriptor()) {
        return failure("iconv does not know the charset '" + charset + "'");
    }
    iconv_close(out);
    return {};
}

bool isValidUtf8(std::string_view text) {
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t extra = 0;
        unsigned code = 0;

        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            code = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            code = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            code = c & 0x07u;
        } else {
            return false;
        }

        if (i + extra >= text.size()) return false;
        for (size_t k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            code = (code << 6) | (cc & 0x3Fu);
        }
        // Overlong sequences and surrogates are not valid UTF-8 either.
        if (extra == 1 && code < 0x80) return false;
        if (extra == 2 && (code < 0x800 || (code >= 0xD800 && code <= 0xDFFF)))
            return false;
        if (extra == 3 && (code < 0x10000 || code > 0x10FFFF)) return false;
        i += extra + 1;
    }
    return true;
}

IconvRecoder::~IconvRecoder() {
    closeDescriptor();
    closeOutDescriptor();
}

IconvRecoder::IconvRecoder(IconvRecoder&& other) noexcept
    : descriptor_(std::exchange(other.descriptor_, nullptr)),
      currentFrom_(std::move(other.currentFrom_)),
      outDescriptor_(std::exchange(other.outDescriptor_, nullptr)),
      currentTo_(std::move(other.currentTo_)) {}

IconvRecoder& IconvRecoder::operator=(IconvRecoder&& other) noexcept {
    if (this != &other) {
        closeDescriptor();
        closeOutDescriptor();
        descriptor_ = std::exchange(other.descriptor_, nullptr);
        currentFrom_ = std::move(other.currentFrom_);
        outDescriptor_ = std::exchange(other.outDescriptor_, nullptr);
        currentTo_ = std::move(other.currentTo_);
    }
    return *this;
}

void IconvRecoder::closeDescriptor() {
    if (descriptor_ != nullptr) {
        iconv_close(asIconv(descriptor_));
        descriptor_ = nullptr;
    }
    currentFrom_.clear();
}

tl::expected<void, ErrorPtr> IconvRecoder::ensureDescriptor(
    const std::string& fromCharset) {
    if (descriptor_ != nullptr && currentFrom_ == fromCharset) return {};
    closeDescriptor();

    // //TRANSLIT keeps characters missing from the target (there are none for
    // UTF-8, but the rule is general) from aborting the conversion.
    const iconv_t cd = iconv_open("UTF-8//TRANSLIT", fromCharset.c_str());
    if (cd == invalidDescriptor()) {
        return failure("iconv does not know the charset '" + fromCharset + "'");
    }
    descriptor_ = reinterpret_cast<void*>(cd);
    currentFrom_ = fromCharset;
    return {};
}

void IconvRecoder::closeOutDescriptor() {
    if (outDescriptor_ != nullptr) {
        iconv_close(asIconv(outDescriptor_));
        outDescriptor_ = nullptr;
    }
    currentTo_.clear();
}

tl::expected<void, ErrorPtr> IconvRecoder::ensureOutDescriptor(
    const std::string& toCharset) {
    if (outDescriptor_ != nullptr && currentTo_ == toCharset) return {};
    closeOutDescriptor();

    // //TRANSLIT lets a character the target has no room for come out as
    // something close — an em dash as a hyphen — rather than as a question
    // mark or an error.
    const iconv_t cd = iconv_open((toCharset + "//TRANSLIT").c_str(), "UTF-8");
    if (cd == invalidDescriptor()) {
        return failure("iconv does not know the charset '" + toCharset + "'");
    }
    outDescriptor_ = reinterpret_cast<void*>(cd);
    currentTo_ = toCharset;
    return {};
}

tl::expected<std::string, ErrorPtr> IconvRecoder::intoCharset(
    std::string_view text, const std::string& toCharset) {
    if (text.empty()) return std::string{};

    if (toCharset == "UTF-8" || toCharset == "UTF8") return std::string(text);
    if (auto opened = ensureOutDescriptor(toCharset); !opened) {
        return tl::make_unexpected(std::move(opened).error());
    }
    iconv(asIconv(outDescriptor_), nullptr, nullptr, nullptr, nullptr);

    std::string out;
    out.reserve(text.size());

    std::vector<char> buffer(4096);
    char* inPtr = const_cast<char*>(text.data());
    size_t inLeft = text.size();

    while (inLeft > 0) {
        char* outPtr = buffer.data();
        size_t outLeft = buffer.size();

        const size_t result =
            iconv(asIconv(outDescriptor_), &inPtr, &inLeft, &outPtr, &outLeft);
        out.append(buffer.data(), buffer.size() - outLeft);

        if (result != static_cast<size_t>(-1)) break;
        if (errno == E2BIG) continue;
        if (errno == EILSEQ || errno == EINVAL) {
            // The character does not exist in the target charset. A question
            // mark is what every FTN editor has always put there.
            out += '?';
            // Step over the whole character: the input is UTF-8, and leaving
            // its continuation bytes behind would turn one bad character into
            // several.
            size_t skip = 1;
            while (skip < inLeft &&
                   (static_cast<unsigned char>(inPtr[skip]) & 0xC0u) == 0x80u) {
                ++skip;
            }
            skip = std::min(skip, inLeft);
            inPtr += skip;
            inLeft -= skip;
            continue;
        }
        return failure("conversion to '" + toCharset + "' failed");
    }
    return out;
}

tl::expected<std::string, ErrorPtr> IconvRecoder::intoUtf8(
    std::string_view text, const std::string& fromCharset) {
    if (text.empty()) return std::string{};

    // Already UTF-8 — nothing to convert; this also rescues messages carrying
    // an incorrect CHRS kludge.
    if (fromCharset == "UTF-8" || fromCharset == "UTF8") {
        if (isValidUtf8(text)) return std::string(text);
    }

    if (auto opened = ensureDescriptor(fromCharset); !opened) {
        return tl::make_unexpected(std::move(opened).error());
    }

    // Reset the converter's state before a new string.
    iconv(asIconv(descriptor_), nullptr, nullptr, nullptr, nullptr);

    std::string out;
    out.reserve(text.size() * 2);

    std::vector<char> buffer(4096);
    // iconv may write into the input buffer for stateful encodings only; ours
    // are single-byte, but the POSIX signature still forces a const_cast.
    char* inPtr = const_cast<char*>(text.data());
    size_t inLeft = text.size();

    while (inLeft > 0) {
        char* outPtr = buffer.data();
        size_t outLeft = buffer.size();

        const size_t result =
            iconv(asIconv(descriptor_), &inPtr, &inLeft, &outPtr, &outLeft);
        out.append(buffer.data(), buffer.size() - outLeft);

        if (result != static_cast<size_t>(-1)) break;

        if (errno == E2BIG) continue;  // output buffer ran out — just go round again
        if (errno == EILSEQ || errno == EINVAL) {
            // The byte does not map into the target charset (or is truncated at the
            // end): substitute U+FFFD and move on — the message beats the byte.
            out.append(kReplacement, sizeof(kReplacement) - 1);
            if (inLeft == 0) break;
            ++inPtr;
            --inLeft;
            continue;
        }
        return failure("conversion from '" + fromCharset + "' failed");
    }
    return out;
}

}  // namespace amberedit::encoding
