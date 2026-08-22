#include "app/compose_prefill.hpp"

namespace amberedit::app {
namespace {

std::string addressText(const domain::FtnAddress& address) {
    return address.isValid() ? address.toString() : "";
}

/// The attributes a message starts with: it was written here, and netmail is
/// private as it has always been. Both are the header screen's to change — the
/// attributes panel under it is where they are set — so they are stated here rather
/// than decided by the base on the way to disk.
uint32_t startingAttributes(bool netmail) {
    return domain::attr::kLocal | (netmail ? domain::attr::kPrivate : 0u);
}

}  // namespace

std::string ownAddress(const config::AppConfig& config, const domain::AreaConfig& area) {
    if (area.address.isValid()) return area.address.toString();
    return config.userAddress ? config.userAddress->toString() : "";
}

ComposeFields newMessage(const config::AppConfig& config,
                         const domain::AreaConfig& area) {
    ComposeFields fields;
    fields.netmail = area.hasAddressedRecipient();
    fields.attributes = startingAttributes(fields.netmail);
    fields.fromName = config.userName;
    fields.fromAddr = ownAddress(config, area);
    // Nobody in particular is being written to in an echo, and "All" is how
    // FTN has always said so. In netmail the recipient is the whole point of
    // the message, so it is left for the user to name.
    if (!fields.netmail) fields.toName = "All";
    return fields;
}

ComposeFields reply(const config::AppConfig& config, const domain::AreaConfig& readIn,
                    const domain::AreaConfig& into,
                    const domain::MessageHeader& header) {
    ComposeFields fields;
    fields.netmail = into.hasAddressedRecipient();
    fields.attributes = startingAttributes(fields.netmail);
    fields.reply = true;
    fields.fromName = config.userName;
    fields.toName = header.from;
    fields.subject = header.subject;

    fields.fromAddr = ownAddress(config, into);
    if (fields.netmail) {
        fields.toAddr = addressText(header.origAddr);
        // The address the message was written to, where the message was written
        // to an address at all and that address is one of ours: a correspondent
        // is answered from the AKA they wrote to, and no [akamatch] pattern gets
        // to second-guess it. Only netmail was — an echo's destination field
        // holds whatever the editor that wrote it left there, and what a tosser
        // leaves there is often this very system, the packet having come
        // addressed here.
        const bool writtenToUs = readIn.hasAddressedRecipient() &&
                                 header.destAddr.isValid() &&
                                 config.isOwnAddress(header.destAddr);
        if (writtenToUs) {
            fields.fromAddr = header.destAddr.toString();
        } else if (header.origAddr.isValid()) {
            // No AKA of ours to keep: an echo answered into netmail, or a
            // netmail that reached us addressed to somebody else. Then the
            // recipient decides which AKA the answer goes out under — the same
            // [akamatch] choice leaving the To address makes in the editor — and
            // where no rule covers them the area's own AKA stays.
            if (const auto aka = senderFor(config, header.origAddr)) {
                fields.fromAddr = *aka;
            }
        }
    }
    return fields;
}

ComposeFields commentReply(const config::AppConfig& config,
                           const domain::AreaConfig& readIn,
                           const domain::AreaConfig& into,
                           const domain::MessageHeader& header) {
    ComposeFields fields = reply(config, readIn, into, header);
    // The one thing that differs from a reply: the message's recipient rather
    // than its sender. Both halves of the row come from them — a name at
    // somebody else's node is not an address anybody can be written to — and an
    // echo's To address addresses nobody, so there is none to take.
    fields.toName = header.to;
    if (fields.netmail) fields.toAddr = addressText(header.destAddr);
    return fields;
}

ComposeFields change(const domain::AreaConfig& area,
                     const domain::MessageHeader& header) {
    ComposeFields fields;
    fields.netmail = area.hasAddressedRecipient();
    fields.changing = true;
    // The attributes the message carries, save for MSGSENT: what went out is not
    // what is being written here, so the message counts as unsent again — and
    // where it is ours to send, a scanner picks it up and sends what it now
    // says. It is an attribute like any other on the header screen from here on,
    // there to be turned back on by anyone who means to.
    fields.attributes = header.attributes & ~domain::attr::kSent;
    fields.fromName = header.from;
    fields.fromAddr = addressText(header.origAddr);
    fields.toName = header.to;
    fields.toAddr = addressText(header.destAddr);
    fields.subject = header.subject;
    return fields;
}

std::optional<std::string> senderFor(const config::AppConfig& config,
                                     const domain::FtnAddress& dest) {
    if (const auto aka = config.akaMatching(dest)) return aka->toString();
    return std::nullopt;
}

}  // namespace amberedit::app
