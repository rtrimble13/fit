#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"
#include "fit/io/table.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <sstream>
#include <string>

using namespace fit;

TEST_CASE("csv: header matching, comments, quoting and blank cells") {
    std::istringstream in("# a comment\n"
                          " ID , Face,Note\r\n"
                          "\n"
                          "A1, 100 ,\"hello, \"\"world\"\"\"\n"
                          "A2,,\n");
    const CsvTable csv = CsvTable::parse(in, "mem.csv");
    REQUIRE(csv.size() == 2);
    CHECK(csv.has_column("face"));
    CHECK(csv.at(0, "id") == "A1");
    CHECK(csv.at(0, "FACE") == "100");
    CHECK(csv.at(0, "note") == "hello, \"world\"");
    CHECK_FALSE(csv.get(1, "face").has_value());
    CHECK_FALSE(csv.get(1, "missing").has_value());
    CHECK(csv.where(1) == "mem.csv:5");
    CHECK_THROWS_WITH(csv.at(1, "face"), "mem.csv:5: missing value for column 'face'");
}

TEST_CASE("csv: structural errors carry file and line") {
    std::istringstream too_wide("a,b\n1,2,3\n");
    CHECK_THROWS_WITH(CsvTable::parse(too_wide, "x.csv"),
                      "x.csv:2: row has 3 fields but the header has 2");
    std::istringstream duplicate("a,A\n");
    CHECK_THROWS_AS(CsvTable::parse(duplicate, "x.csv"), Error);
    std::istringstream empty("# nothing\n");
    CHECK_THROWS_AS(CsvTable::parse(empty, "x.csv"), Error);
    std::istringstream unterminated("a\n\"oops\n");
    CHECK_THROWS_AS(CsvTable::parse(unterminated, "x.csv"), Error);
    std::istringstream misspelt("id,faec\n");
    CHECK_THROWS_WITH_AS(CsvTable::parse(misspelt, "x.csv").require_columns_in({"id", "face"}),
                         doctest::Contains("unknown column 'faec'"), Error);
    CHECK_THROWS_AS(CsvTable::read("/no/such/file.csv"), Error);
}

TEST_CASE("parsing helpers") {
    CHECK(parse_double(" 4.25 ", "coupon") == 4.25);
    CHECK(parse_double("+25", "shift") == 25.0);
    CHECK(parse_double("-1e-3", "x") == -0.001);
    CHECK_THROWS_WITH(parse_double("4.25%", "coupon"), "invalid number '4.25%' for coupon");
    CHECK_THROWS_AS(parse_double("", "x"), Error);
    CHECK_THROWS_AS(parse_double("nan", "x"), Error);
    CHECK(parse_int("12", "n") == 12);
    CHECK_THROWS_AS(parse_int("1.5", "n"), Error);
    CHECK(split_list(" 2Y, 5Y ,,10Y ") == std::vector<std::string>{"2Y", "5Y", "10Y"});
    CHECK(split_list("2Y:-25/10Y:25", '/').size() == 2);
}

TEST_CASE("table: number formatting") {
    CHECK(format_number(1234567.891, 2, true) == "1,234,567.89");
    CHECK(format_number(-1234.5, 1, true) == "-1,234.5");
    CHECK(format_number(999.0, 0, true) == "999");
    CHECK(format_number(-0.00001, 2) == "0.00");
    CHECK(format_number(std::nan(""), 2) == "n/a");
}

TEST_CASE("table: text, csv and json renderings") {
    Report report;
    Table& kv = report.add(Table::key_value("summary"));
    kv.add("as_of", "2026-11-17");
    kv.add("value", 1234.5678, 2, true);
    Table& t = report.add(Table("rows"));
    t.column("id").column("px", 2);
    t.row({std::string("A,1"), 99.123456789});
    t.row({std::string("B"), std::nan("")});

    std::ostringstream text;
    report.render(text, OutputFormat::Table, {});
    CHECK(text.str() == "summary\n=======\nas_of  2026-11-17\nvalue    1,234.57\n\n"
                        "rows\n====\nid      px\n---  -----\nA,1  99.12\nB      n/a\n");

    std::ostringstream precise;
    report.render(precise, OutputFormat::Table, 4);
    CHECK(precise.str().find("99.1235") != std::string::npos);

    // Machine-readable formats are never rounded.
    std::ostringstream csv;
    report.render(csv, OutputFormat::Csv, {});
    CHECK(csv.str() == "# summary\nkey,value\nas_of,2026-11-17\nvalue,1234.5678\n\n"
                       "# rows\nid,px\n\"A,1\",99.123456789\nB,\n");

    std::ostringstream json;
    report.render(json, OutputFormat::Json, {});
    CHECK(json.str() == "{\n  \"summary\": {\n    \"as_of\": \"2026-11-17\",\n"
                        "    \"value\": 1234.5678\n  },\n  \"rows\": [\n"
                        "    {\"id\": \"A,1\", \"px\": 99.123456789},\n"
                        "    {\"id\": \"B\", \"px\": null}\n  ]\n}\n");
    CHECK_THROWS_AS(parse_output_format("xml"), UsageError);
}
