#include "app/url_handler.hpp"

#include <string_view>

#include "app/run_program.hpp"
#include "config/app_config.hpp"

namespace amberedit::app {

std::vector<std::string> urlHandlerCommand(const std::vector<std::string>& handler,
                                           const std::string& url) {
    const std::string_view mark = config::AppConfig::kUrlPlaceholder;
    std::vector<std::string> command;
    command.reserve(handler.size());
    for (const std::string& word : handler) {
        std::string filled = word;
        for (size_t at = filled.find(mark); at != std::string::npos;
             at = filled.find(mark, at + url.size())) {
            filled.replace(at, mark.size(), url);
        }
        command.push_back(std::move(filled));
    }
    return command;
}

tl::expected<void, ErrorPtr> runUrlHandler(const std::vector<std::string>& handler,
                                           const std::string& url) {
    // The link is one argument of the exec `runProgram` does and never reaches
    // a shell — see the header for why that is what makes the quoting question
    // not arise.
    return runProgram(urlHandlerCommand(handler, url));
}

}  // namespace amberedit::app
