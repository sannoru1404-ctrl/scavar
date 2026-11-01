#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace Scavar
{
// Minimal RFC-4180 style CSV: UTF-8, optional BOM, quoted fields, CRLF/LF.
struct CsvTable
{
    std::vector<std::string> Header;
    std::vector<std::vector<std::string>> Rows;

    int ColumnIndex(std::string_view name) const;
    // Returns empty string when the column is missing.
    const std::string& Cell(size_t row, std::string_view column) const;
};

bool ParseCsv(std::string_view text, CsvTable& out, std::string* error);
bool LoadCsvFile(const std::string& path, CsvTable& out, std::string* error);
} // namespace Scavar
