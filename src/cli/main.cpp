#include "commands.hpp"
#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"
#include "fit/version.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using fit::cli::Command;
using fit::cli::OptionSpec;

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

const std::vector<OptionSpec> kOutputOptions = {
    {"format", "FMT", "output format: table | csv | json", "table", false},
    {"precision", "N", "decimal places for fractional numbers in table output", "", false},
    {"help", "", "show this help", "", false},
};

std::vector<Command> all_commands() {
    std::vector<Command> commands = fit::cli::curve_commands();
    for (auto& c : fit::cli::bond_commands()) {
        commands.push_back(std::move(c));
    }
    for (auto& c : fit::cli::portfolio_commands()) {
        commands.push_back(std::move(c));
    }
    return commands;
}

std::string invocation(const Command& c) {
    return "fit " + c.group + (c.name.empty() ? "" : " " + c.name);
}

void print_overview(std::ostream& out, const std::vector<Command>& commands,
                    const std::string& group = "") {
    if (group.empty()) {
        out << "fit " << fit::kVersion << " - fixed income toolkit\n\n"
            << "Usage: fit <command> [options]\n\nCommands:\n";
    } else {
        out << "Usage: fit " << group << " <command> [options]\n\nCommands:\n";
    }
    for (const Command& c : commands) {
        if (group.empty() || c.group == group) {
            std::string name = invocation(c).substr(4);
            name.resize(std::max<std::size_t>(name.size(), 20), ' ');
            out << "  " << name << c.summary << '\n';
        }
    }
    out << "\nRun 'fit <command> --help' for the options of a command.\n"
        << "Rates, coupons and yields are in percent; spreads and shifts in basis points;\n"
        << "prices per 100 face; dates YYYY-MM-DD; tenors like 3M or 10Y.\n";
}

void print_command_help(std::ostream& out, const Command& c) {
    out << c.summary << "\n\nUsage: " << invocation(c) << " [options]\n\nOptions:\n";
    for (const OptionSpec& o : c.options + kOutputOptions) {
        std::string left = "  --" + o.name + (o.value_name.empty() ? "" : " " + o.value_name);
        left.resize(std::max<std::size_t>(left.size(), 26), ' ');
        out << left << ' ' << o.help;
        if (o.required) {
            out << " (required)";
        } else if (!o.default_value.empty()) {
            out << " [" << o.default_value << "]";
        }
        out << '\n';
    }
    out << "\nExample:\n  " << c.example << '\n';
}

int run(const std::vector<std::string>& argv) {
    const std::vector<Command> commands = all_commands();
    if (argv.empty() || argv[0] == "--help" || argv[0] == "-h" || argv[0] == "help") {
        print_overview(std::cout, commands);
        return argv.empty() ? kExitUsage : kExitOk;
    }
    if (argv[0] == "--version" || argv[0] == "version") {
        std::cout << "fit " << fit::kVersion << '\n';
        return kExitOk;
    }

    const std::string& group = argv[0];
    const Command* command = nullptr;
    std::size_t consumed = 1;
    bool group_exists = false;
    for (const Command& c : commands) {
        if (c.group != group) {
            continue;
        }
        group_exists = true;
        if (c.name.empty()) {
            command = &c;
        } else if (argv.size() > 1 && argv[1] == c.name) {
            command = &c;
            consumed = 2;
        }
    }
    if (!group_exists) {
        throw fit::UsageError("unknown command '" + group + "' (run 'fit --help')");
    }
    if (command == nullptr) {
        if (argv.size() > 1 && argv[1] != "--help" && argv[1] != "-h") {
            throw fit::UsageError("unknown command '" + group + " " + argv[1] + "' (run 'fit " +
                                  group + " --help')");
        }
        print_overview(std::cout, commands, group);
        return argv.size() > 1 ? kExitOk : kExitUsage;
    }

    const std::vector<std::string> tokens(argv.begin() + static_cast<long>(consumed), argv.end());
    for (const std::string& token : tokens) {
        if (token == "--help" || token == "-h") {
            print_command_help(std::cout, *command);
            return kExitOk;
        }
    }
    const fit::cli::Args args(command->options + kOutputOptions, tokens);
    const fit::OutputFormat format = fit::parse_output_format(args.require("format"));
    std::optional<int> precision;
    if (const auto p = args.get("precision")) {
        precision = fit::parse_int(*p, "--precision");
        if (*precision < 0 || *precision > 15) {
            throw fit::UsageError("--precision must be between 0 and 15");
        }
    }

    // Render into a buffer so a failing command never emits partial output on stdout.
    fit::Report report;
    command->run(args, report);
    std::ostringstream buffer;
    report.render(buffer, format, precision);
    std::cout << buffer.str();
    for (const std::string& warning : report.warnings()) {
        std::cerr << "fit: warning: " << warning << '\n';
    }
    return kExitOk;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(std::vector<std::string>(argv + 1, argv + argc));
    } catch (const fit::UsageError& e) {
        std::cerr << "fit: error: " << e.what() << '\n';
        return kExitUsage;
    } catch (const fit::Error& e) {
        std::cerr << "fit: error: " << e.what() << '\n';
        return kExitError;
    } catch (const std::exception& e) {
        std::cerr << "fit: internal error: " << e.what() << '\n';
        return kExitError;
    }
}
