#pragma once

#include <string_view>
#include <span>
#include <optional>
#include <unordered_map>


class ArgumentParser
{
public:
    ArgumentParser(int argc, char** argv);

    std::string_view                program_name() const noexcept;
    bool                            has(std::string_view opt) const noexcept;
    std::optional<std::string_view> get(std::string_view opt) const noexcept;

private:
    std::span<char*>                                                      raw_args_;
    std::unordered_map<std::string_view, std::optional<std::string_view>> options_;

private:
    void parse();
};
