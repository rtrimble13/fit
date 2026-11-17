#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fit {

/// Parses a decimal number, rejecting trailing garbage. `what` names the value in errors.
double parse_double(std::string_view text, const std::string& what);
int parse_int(std::string_view text, const std::string& what);
/// Splits on `separator`, trimming whitespace and dropping empty items.
std::vector<std::string> split_list(std::string_view text, char separator = ',');
std::string trim(std::string_view text);
std::string to_lower(std::string_view text);

/// A CSV file with a header row.
///
/// Supports RFC-4180 quoting, blank lines and full-line `#` comments. Column names are
/// case-insensitive. Every error message carries `file:line`.
class CsvTable {
public:
    static CsvTable read(const std::string& path);
    static CsvTable parse(std::istream& in, const std::string& source);

    std::size_t size() const { return rows_.size(); }
    const std::vector<std::string>& columns() const { return columns_; }
    bool has_column(std::string_view name) const;

    /// The cell, or nullopt when the column is absent or the cell is empty.
    std::optional<std::string> get(std::size_t row, std::string_view column) const;
    /// The cell; throws when the column is absent or the cell is empty.
    std::string at(std::size_t row, std::string_view column) const;
    /// "file:line" of a data row, for error messages.
    std::string where(std::size_t row) const;
    /// Throws unless every column name is in `allowed` (catches misspelt headers).
    void require_columns_in(const std::vector<std::string>& allowed) const;

private:
    std::string source_;
    std::vector<std::string> columns_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<int> lines_;
};

} // namespace fit
