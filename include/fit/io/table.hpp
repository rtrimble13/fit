#pragma once

#include <deque>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fit {

enum class OutputFormat { Table, Csv, Json };

OutputFormat parse_output_format(std::string_view text);

/// A table cell: text or a number. NaN renders as "n/a" / empty / null.
using Cell = std::variant<std::string, double>;

/// A named result table that can be rendered as aligned text, CSV or JSON.
///
/// Decimal places apply to the human-readable text rendering only; CSV and JSON always
/// carry full precision so downstream tools never see rounded numbers.
class Table {
public:
    /// A regular table with a header row.
    explicit Table(std::string name) : name_(std::move(name)) {}
    /// A two-column key/value table; rendered as an object in JSON.
    static Table key_value(std::string name);

    /// Adds a column. `decimals < 0` marks a text column; `grouped` adds thousands
    /// separators in the text rendering.
    Table& column(std::string key, int decimals = -1, bool grouped = false);
    Table& row(std::vector<Cell> cells);

    /// Adds an entry to a key/value table.
    Table& add(std::string key, double value, int decimals, bool grouped = false);
    Table& add(std::string key, std::string value);

    const std::string& name() const { return name_; }
    bool is_key_value() const { return key_value_; }

    void render_text(std::ostream& out, std::optional<int> precision) const;
    void render_csv(std::ostream& out) const;
    void render_json(std::ostream& out, int indent) const;

private:
    struct Column {
        std::string key;
        int decimals;
        bool grouped;
    };
    struct Entry {
        std::string key;
        Cell value;
        int decimals;
        bool grouped;
    };

    std::string name_;
    bool key_value_ = false;
    std::vector<Column> columns_;
    std::vector<std::vector<Cell>> rows_;
    std::vector<Entry> entries_;
};

/// The ordered set of tables a command produces.
class Report {
public:
    Table& add(Table table);
    /// Diagnostics shown on stderr after the output (never part of stdout).
    void warn(std::string message) { warnings_.push_back(std::move(message)); }
    const std::vector<std::string>& warnings() const { return warnings_; }

    void render(std::ostream& out, OutputFormat format, std::optional<int> precision) const;

private:
    std::deque<Table> tables_; // deque: add() returns stable references
    std::vector<std::string> warnings_;
};

/// Formats a number with fixed decimals and optional thousands separators.
std::string format_number(double value, int decimals, bool grouped = false);

} // namespace fit
