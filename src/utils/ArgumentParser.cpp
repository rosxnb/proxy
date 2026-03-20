#include <utils/ArgumetParser.hpp>


ArgumentParser::ArgumentParser(int argc, char** argv)
    : raw_args_(argv, argc)
{
    parse();
}

void
ArgumentParser::parse()
{
    for (size_t i = 0; i < raw_args_.size(); ++i) {
        std::string_view current = raw_args_[i];

        if (current.starts_with("--")) {
            if (i + 1 < raw_args_.size() &&
                !std::string_view(raw_args_[i + 1]).starts_with("--"))
            {
                options_[current] = raw_args_[i + 1];
            }
            else {
                options_[current] = std::nullopt;
            }
        }
    }
}

std::string_view
ArgumentParser::program_name() const noexcept
{
    return raw_args_.empty()
        ? ""
        : raw_args_[0];
}

bool
ArgumentParser::has(std::string_view opt) const noexcept
{
    return options_.contains(opt);
}

std::optional<std::string_view>
ArgumentParser::get(std::string_view opt) const noexcept
{
    auto it = options_.find(opt);
    return it != options_.end()
        ? it->second
        : std::nullopt;
}
