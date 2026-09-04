#include "app/run_program.hpp"

#include "i18n/i18n.hpp"
#include "sys/process.hpp"

namespace amberedit::app {

tl::expected<void, ErrorPtr> runProgram(const std::vector<std::string>& command) {
    if (command.empty()) return {};

    const std::string& program = command.front();
    const sys::CommandResult result = sys::runCommand(command);

    switch (result.stage) {
        case sys::CommandStage::Started:
            return {};
        case sys::CommandStage::NotStarted:
            return failure(i18n::format(_("cannot run {0}: {1}"), {program, result.reason}));
        case sys::CommandStage::NotWaited:
            return failure(
                i18n::format(_("cannot wait for {0}: {1}"), {program, result.reason}));
    }
    return {};
}

}  // namespace amberedit::app
