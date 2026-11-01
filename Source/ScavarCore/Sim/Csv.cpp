#include "Csv.h"
#include <fstream>
#include <sstream>

namespace Scavar
{
int CsvTable::ColumnIndex(std::string_view name) const
{
    for (size_t i = 0; i < Header.size(); ++i)
        if (Header[i] == name) return static_cast<int>(i);
    return -1;
}

const std::string& CsvTable::Cell(size_t row, std::string_view column) const
{
    static const std::string Empty;
    const int c = ColumnIndex(column);
    if (c < 0 || row >= Rows.size() || static_cast<size_t>(c) >= Rows[row].size()) return Empty;
    return Rows[row][static_cast<size_t>(c)];
}

bool ParseCsv(std::string_view text, CsvTable& out, std::string* error)
{
    out = CsvTable{};
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);

    std::vector<std::vector<std::string>> records;
    std::vector<std::string> record;
    std::string field;
    bool inQuotes = false, fieldStarted = false;

    auto endField = [&] { record.push_back(std::move(field)); field.clear(); fieldStarted = false; };
    auto endRecord = [&] {
        endField();
        if (!(record.size() == 1 && record[0].empty())) records.push_back(std::move(record));
        record.clear();
    };

    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (inQuotes)
        {
            if (c == '"')
            {
                if (i + 1 < text.size() && text[i + 1] == '"') { field.push_back('"'); ++i; }
                else inQuotes = false;
            }
            else field.push_back(c);
        }
        else if (c == '"' && !fieldStarted) { inQuotes = true; fieldStarted = true; }
        else if (c == ',') endField();
        else if (c == '\n') endRecord();
        else if (c == '\r') { if (i + 1 < text.size() && text[i + 1] == '\n') ++i; endRecord(); }
        else { field.push_back(c); fieldStarted = true; }
    }
    if (inQuotes) { if (error) *error = "unterminated quoted field"; return false; }
    if (fieldStarted || !record.empty() || !field.empty()) endRecord();

    if (records.empty()) { if (error) *error = "empty csv"; return false; }
    out.Header = std::move(records.front());
    for (size_t r = 1; r < records.size(); ++r)
    {
        if (records[r].size() != out.Header.size())
        {
            if (error) *error = "row " + std::to_string(r + 1) + " has " + std::to_string(records[r].size()) + " fields, expected " + std::to_string(out.Header.size());
            return false;
        }
        out.Rows.push_back(std::move(records[r]));
    }
    return true;
}

bool LoadCsvFile(const std::string& path, CsvTable& out, std::string* error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    if (!ParseCsv(text, out, error)) { if (error) *error = path + ": " + *error; return false; }
    return true;
}
} // namespace Scavar
