#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domain/ftn_address.hpp"

namespace amberedit::domain {

/// Message base storage type. The MVP reads Squish, JAM and Fido *.msg; the
/// remaining values exist so that tosser configs parse without losing
/// information.
enum class MsgBaseType {
    Unknown,
    Squish,
    Jam,
    Sdm,          ///< Fido *.msg, FTS-0001
    Passthrough,  ///< no base on disk
};

std::string nameOf(MsgBaseType type);

/// The base type a word names, or nothing where it names none of them. The
/// words `nameOf` writes, read without regard to case, and the two aliases a
/// tosser config may spell the Fido *.msg base with — `sdm` and `fido`.
///
/// Beside `nameOf` because it is its inverse: the one place that knows what a
/// base type is called, so that a word AmberEdit's own config accepts is a word
/// it can also print back.
std::optional<MsgBaseType> parseMsgBaseType(std::string_view word);

/// Area kind as the tosser sees it.
enum class AreaKind {
    Echo,
    Netmail,
    Local,
    Bad,
    Dupe,
};

std::string nameOf(AreaKind kind);

/// The area kind a word names, or nothing where it names none of them — the
/// words `nameOf` writes, read without regard to case.
std::optional<AreaKind> parseAreaKind(std::string_view word);

/// An area as described by the tosser config. This is the unit of work for
/// IMsgBase/ILastReadStore: an adapter takes one and opens the base it names.
struct AreaConfig {
    std::string tag;   ///< echo tag, e.g. "ru.linux"
    std::string path;  ///< base path without extension (Squish/JAM)
    MsgBaseType type{MsgBaseType::Unknown};
    AreaKind kind{AreaKind::Echo};
    std::string description;
    std::string group;
    /// The AKA the tosser presents this area under. squish.cfg states it with
    /// `-p`; the other formats leave it unset, so check isValid() before use.
    FtnAddress address;
    std::vector<FtnAddress> links;

    [[nodiscard]] bool isPassthrough() const {
        return type == MsgBaseType::Passthrough || path.empty();
    }

    /// Whether a message's destination address names an actual recipient.
    /// Only netmail is addressed to a node. Echomail is broadcast to the area,
    /// so its destination field holds whatever the writing editor happened to
    /// leave there and means nothing to a reader.
    [[nodiscard]] bool hasAddressedRecipient() const { return kind == AreaKind::Netmail; }
};

}  // namespace amberedit::domain
