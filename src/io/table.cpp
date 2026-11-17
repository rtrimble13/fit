#include "fit/io/table.hpp"

#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ostream>

namespace fit {

namespace {

/// Shortest representation that round-trips to the same double at 12 significant digits.
std::string full_precision(double value) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.12g", value);
    return buffer;
}

std::string csv_escape(const std::string& text) {
    if (text.find_first_of(",\"\n") == std::string::npos) {
        return text;
    }
    std::string out = "\"";
    for (const char c : text) {
        out += c;
        if (c == '"') {
            out += '"';
        }
    }
    return out + "\"";
}

std::string json_string(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

std::string text_cell(const Cell& cell, int decimals, bool grouped, std::optional<int> precision) {
    if (const auto* text = std::get_if<std::string>(&cell)) {
        return *text;
    }
    const double value = std::get<double>(cell);
    const int dp = precision && decimals > 0 ? *precision : std::max(decimals, 0);
    return format_number(value, dp, grouped);
}

std::string csv_cell(const Cell& cell) {
    if (const auto* text = std::get_if<std::string>(&cell)) {
        return csv_escape(*text);
    }
    const double value = std::get<double>(cell);
    return std::isnan(value) ? "" : full_precision(value);
}

std::string json_cell(const Cell& cell) {
    if (const auto* text = std::get_if<std::string>(&cell)) {
        return json_string(*text);
    }
    const double value = std::get<double>(cell);
    return std::isfinite(value) ? full_precision(value) : "null";
}

void write_aligned(std::ostream& out, const std::vector<std::vector<std::string>>& lines,
                   const std::vector<bool>& right_align, bool underline_header) {
    std::vector<std::size_t> widths(right_align.size(), 0);
    for (const auto& line : lines) {
        for (std::size_t c = 0; c < line.size(); ++c) {
            widths[c] = std::max(widths[c], line[c].size());
        }
    }
    const auto write_line = [&](const std::vector<std::string>& line) {
        std::string text;
        for (std::size_t c = 0; c < line.size(); ++c) {
            const std::string pad(widths[c] - line[c].size(), ' ');
            text += (c == 0 ? "" : "  ");
            text += right_align[c] ? pad + line[c] : line[c] + pad;
        }
        while (!text.empty() && text.back() == ' ') {
            text.pop_back();
        }
        out << text << '\n';
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        write_line(lines[i]);
        if (i == 0 && underline_header) {
            std::vector<std::string> rule;
            for (const std::size_t w : widths) {
                rule.emplace_back(w, '-');
            }
            write_line(rule);
        }
    }
}

} // namespace

OutputFormat parse_output_format(std::string_view text) {
    const std::string t = to_lower(text);
    if (t == "table" || t == "text") {
        return OutputFormat::Table;
    }
    if (t == "csv") {
        return OutputFormat::Csv;
    }
    if (t == "json") {
        return OutputFormat::Json;
    }
    throw UsageError("unknown output format '" + std::string(text) +
                     "' (expected table, csv or json)");
}

std::string format_number(double value, int decimals, bool grouped) {
    if (std::isnan(value)) {
        return "n/a";
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    std::string text = buffer;
    // Avoid "-0.00".
    if (text.front() == '-' && text.find_first_not_of("-0.") == std::string::npos) {
        text.erase(0, 1);
    }
    if (!grouped) {
        return text;
    }
    const std::size_t start = text.front() == '-' ? 1 : 0;
    std::size_t end = text.find('.');
    if (end == std::string::npos) {
        end = text.size();
    }
    for (std::size_t pos = end; pos > start + 3; pos -= 3) {
        text.insert(pos - 3, ",");
    }
    return text;
}

Table Table::key_value(std::string name) {
    Table t(std::move(name));
    t.key_value_ = true;
    return t;
}

Table& Table::column(std::string key, int decimals, bool grouped) {
    columns_.push_back({std::move(key), decimals, grouped});
    return *this;
}

Table& Table::row(std::vector<Cell> cells) {
    require(cells.size() == columns_.size(), "table '" + name_ + "': row width mismatch");
    rows_.push_back(std::move(cells));
    return *this;
}

Table& Table::add(std::string key, double value, int decimals, bool grouped) {
    entries_.push_back({std::move(key), value, decimals, grouped});
    return *this;
}

Table& Table::add(std::string key, std::string value) {
    entries_.push_back({std::move(key), std::move(value), -1, false});
    return *this;
}

void Table::render_text(std::ostream& out, std::optional<int> precision) const {
    out << name_ << '\n' << std::string(name_.size(), '=') << '\n';
    std::vector<std::vector<std::string>> lines;
    if (key_value_) {
        for (const Entry& e : entries_) {
            lines.push_back({e.key, text_cell(e.value, e.decimals, e.grouped, precision)});
        }
        write_aligned(out, lines, {false, true}, false);
        return;
    }
    std::vector<bool> right;
    std::vector<std::string> header;
    for (const Column& c : columns_) {
        header.push_back(c.key);
        right.push_back(c.decimals >= 0);
    }
    lines.push_back(header);
    for (const auto& r : rows_) {
        std::vector<std::string> line;
        for (std::size_t c = 0; c < r.size(); ++c) {
            line.push_back(text_cell(r[c], columns_[c].decimals, columns_[c].grouped, precision));
        }
        lines.push_back(std::move(line));
    }
    write_aligned(out, lines, right, true);
}

void Table::render_csv(std::ostream& out) const {
    if (key_value_) {
        out << "key,value\n";
        for (const Entry& e : entries_) {
            out << csv_escape(e.key) << ',' << csv_cell(e.value) << '\n';
        }
        return;
    }
    for (std::size_t c = 0; c < columns_.size(); ++c) {
        out << (c == 0 ? "" : ",") << csv_escape(columns_[c].key);
    }
    out << '\n';
    for (const auto& r : rows_) {
        for (std::size_t c = 0; c < r.size(); ++c) {
            out << (c == 0 ? "" : ",") << csv_cell(r[c]);
        }
        out << '\n';
    }
}

void Table::render_json(std::ostream& out, int indent) const {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    if (key_value_) {
        out << "{\n";
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            out << pad << "  " << json_string(entries_[i].key) << ": "
                << json_cell(entries_[i].value) << (i + 1 < entries_.size() ? "," : "") << '\n';
        }
        out << pad << "}";
        return;
    }
    out << "[\n";
    for (std::size_t r = 0; r < rows_.size(); ++r) {
        out << pad << "  {";
        for (std::size_t c = 0; c < columns_.size(); ++c) {
            out << (c == 0 ? "" : ", ") << json_string(columns_[c].key) << ": "
                << json_cell(rows_[r][c]);
        }
        out << "}" << (r + 1 < rows_.size() ? "," : "") << '\n';
    }
    out << pad << "]";
}

Table& Report::add(Table table) {
    tables_.push_back(std::move(table));
    return tables_.back();
}

void Report::render(std::ostream& out, OutputFormat format, std::optional<int> precision) const {
    switch (format) {
    case OutputFormat::Table:
        for (std::size_t i = 0; i < tables_.size(); ++i) {
            if (i > 0) {
                out << '\n';
            }
            tables_[i].render_text(out, precision);
        }
        break;
    case OutputFormat::Csv:
        for (std::size_t i = 0; i < tables_.size(); ++i) {
            if (tables_.size() > 1) {
                out << (i > 0 ? "\n" : "") << "# " << tables_[i].name() << '\n';
            }
            tables_[i].render_csv(out);
        }
        break;
    case OutputFormat::Json:
        out << "{\n";
        for (std::size_t i = 0; i < tables_.size(); ++i) {
            out << "  " << json_string(tables_[i].name()) << ": ";
            tables_[i].render_json(out, 2);
            out << (i + 1 < tables_.size() ? "," : "") << '\n';
        }
        out << "}\n";
        break;
    }
}

} // namespace fit
