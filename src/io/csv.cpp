#include "fit/io/csv.hpp"

#include "fit/core/error.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <istream>

namespace fit {

std::string trim(std::string_view text) {
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!text.empty() && is_space(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

std::string to_lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

double parse_double(std::string_view text, const std::string& what) {
    std::string t = trim(text);
    if (!t.empty() && t.front() == '+') {
        t.erase(0, 1);
    }
    double value = 0.0;
    const auto* end = t.data() + t.size();
    const auto result = std::from_chars(t.data(), end, value);
    require(!t.empty() && result.ec == std::errc{} && result.ptr == end && std::isfinite(value),
            "invalid number '" + std::string(text) + "' for " + what);
    return value;
}

int parse_int(std::string_view text, const std::string& what) {
    const std::string t = trim(text);
    int value = 0;
    const auto* end = t.data() + t.size();
    const auto result = std::from_chars(t.data(), end, value);
    require(!t.empty() && result.ec == std::errc{} && result.ptr == end,
            "invalid integer '" + std::string(text) + "' for " + what);
    return value;
}

std::vector<std::string> split_list(std::string_view text, char separator) {
    std::vector<std::string> items;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = std::min(text.find(separator, begin), text.size());
        std::string item = trim(text.substr(begin, end - begin));
        if (!item.empty()) {
            items.push_back(std::move(item));
        }
        begin = end + 1;
    }
    return items;
}

namespace {

std::vector<std::string> split_csv_line(const std::string& line, const std::string& where) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    bool was_quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
            was_quoted = true;
        } else if (c == ',') {
            fields.push_back(was_quoted ? field : trim(field));
            field.clear();
            was_quoted = false;
        } else {
            field += c;
        }
    }
    require(!quoted, where + ": unterminated quoted field");
    fields.push_back(was_quoted ? field : trim(field));
    return fields;
}

} // namespace

CsvTable CsvTable::read(const std::string& path) {
    std::ifstream in(path);
    require(in.good(), "cannot open '" + path + "'");
    return parse(in, path);
}

CsvTable CsvTable::parse(std::istream& in, const std::string& source) {
    CsvTable table;
    table.source_ = source;
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string stripped = trim(line);
        if (stripped.empty() || stripped.front() == '#') {
            continue;
        }
        const std::string where = source + ":" + std::to_string(line_no);
        std::vector<std::string> fields = split_csv_line(line, where);
        if (table.columns_.empty()) {
            for (std::string& f : fields) {
                f = to_lower(f);
                require(!f.empty(), where + ": empty column name in header");
            }
            for (std::size_t i = 0; i < fields.size(); ++i) {
                require(std::find(fields.begin(), fields.begin() + static_cast<long>(i),
                                  fields[i]) == fields.begin() + static_cast<long>(i),
                        where + ": duplicate column '" + fields[i] + "'");
            }
            table.columns_ = std::move(fields);
            continue;
        }
        require(fields.size() <= table.columns_.size(),
                where + ": row has " + std::to_string(fields.size()) +
                    " fields but the header has " + std::to_string(table.columns_.size()));
        fields.resize(table.columns_.size());
        table.rows_.push_back(std::move(fields));
        table.lines_.push_back(line_no);
    }
    require(!table.columns_.empty(), source + ": file is empty (a header row is required)");
    return table;
}

bool CsvTable::has_column(std::string_view name) const {
    return std::find(columns_.begin(), columns_.end(), to_lower(name)) != columns_.end();
}

std::optional<std::string> CsvTable::get(std::size_t row, std::string_view column) const {
    const auto it = std::find(columns_.begin(), columns_.end(), to_lower(column));
    if (it == columns_.end()) {
        return std::nullopt;
    }
    const std::string& cell = rows_.at(row)[static_cast<std::size_t>(it - columns_.begin())];
    if (cell.empty()) {
        return std::nullopt;
    }
    return cell;
}

std::string CsvTable::at(std::size_t row, std::string_view column) const {
    auto cell = get(row, column);
    require(cell.has_value(),
            where(row) + ": missing value for column '" + std::string(column) + "'");
    return *cell;
}

std::string CsvTable::where(std::size_t row) const {
    return source_ + ":" + std::to_string(lines_.at(row));
}

void CsvTable::require_columns_in(const std::vector<std::string>& allowed) const {
    for (const std::string& column : columns_) {
        if (std::find(allowed.begin(), allowed.end(), column) == allowed.end()) {
            std::string list;
            for (const std::string& a : allowed) {
                list += (list.empty() ? "" : ", ") + a;
            }
            throw Error(source_ + ": unknown column '" + column + "' (expected: " + list + ")");
        }
    }
}

} // namespace fit
