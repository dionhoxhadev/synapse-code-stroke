#include "clinic.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

static const int NORM_FIELDS = 6;
static const size_t MAX_NUMBER_LENGTH = 9;

const std::vector<ClinicTest>& clinicTests()
{
    static const std::vector<ClinicTest> tests = {
        {TestKind::Reaction, "reaction", "Reaction", "Reaction time", true, 20,
         {"right-arm-face", "awake"}},
        {TestKind::Memory, "memory", "Memory", "Memory span", false, 1,
         {"memory", "understanding"}},
        {TestKind::ColorWord, "colorword", "Colour words", "Colour words", true, 1,
         {"understanding", "left-attention", "right-vision", "left-vision"}},
    };
    return tests;
}

const ClinicTest* findTest(const std::string& code)
{
    for (const auto& test : clinicTests())
        if (test.code == code) return &test;
    return nullptr;
}

bool parseWhole(const std::string& text, int& value)
{
    if (text.empty() || text.size() > MAX_NUMBER_LENGTH) return false;
    for (unsigned char c : text)
        if (!std::isdigit(c)) return false;
    value = std::stoi(text);
    return true;
}

bool parseDecimal(const std::string& text, double& value)
{
    if (text.empty() || text.size() > MAX_NUMBER_LENGTH || text.front() == '.' || text.back() == '.')
        return false;
    int points = 0;
    for (unsigned char c : text) {
        if (c == '.') ++points;
        else if (!std::isdigit(c)) return false;
    }
    if (points > 1) return false;
    value = std::strtod(text.c_str(), nullptr);
    return true;
}

double oneDecimal(double value)
{
    return std::round(value * 10) / 10;
}

// Reads one NORM line. Returns false, with the problems added, if it is wrong.
static bool readNorm(const std::vector<std::string>& fields, int lineNo, Norm& norm,
                     std::vector<AtlasError>& errors)
{
    auto problem = [&](const std::string& text) { errors.push_back({lineNo, text}); };
    size_t before = errors.size();

    const ClinicTest* test = findTest(fields[1]);
    if (test) norm.kind = test->kind;
    else problem("unknown test " + inQuotes(fields[1]) + ". Expected reaction, memory or colorword.");

    bool agesRead = parseWhole(fields[2], norm.fromAge) && parseWhole(fields[3], norm.toAge);
    if (!agesRead) problem("ages must be whole numbers.");
    else if (norm.fromAge > norm.toAge)
        problem("ages " + fields[2] + " to " + fields[3] + " are the wrong way round.");

    if (!parseDecimal(fields[4], norm.average) || norm.average <= 0)
        problem("the normal average must be a number above 0, found " + inQuotes(fields[4]) + ".");
    if (!parseDecimal(fields[5], norm.spread) || norm.spread <= 0)
        problem("the spread must be a number above 0, found " + inQuotes(fields[5]) + ".");

    norm.line = lineNo;
    return errors.size() == before;
}

bool NormTable::load(const std::string& path, NormTable& out, std::vector<AtlasError>& errors)
{
    errors.clear();
    std::ifstream f(path);
    if (!f) {
        errors.push_back({0, "File not found: " + path});
        return false;
    }

    NormTable table;
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (lineNo == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);   // BOM
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> fields = split(line, '|');
        if (fields[0] != "NORM") {
            errors.push_back({lineNo, "unknown record type " + inQuotes(fields[0]) + ". Expected NORM."});
            continue;
        }
        if ((int)fields.size() != NORM_FIELDS) {
            errors.push_back({lineNo, "NORM needs " + std::to_string(NORM_FIELDS) + " fields, found " +
                                      std::to_string(fields.size()) + "."});
            continue;
        }

        Norm norm;
        if (!readNorm(fields, lineNo, norm, errors)) continue;
        for (const auto& other : table.norms_)
            if (other.kind == norm.kind && norm.fromAge <= other.toAge && other.fromAge <= norm.toAge) {
                errors.push_back({lineNo, "ages " + fields[2] + " to " + fields[3] +
                                          " overlap with line " + std::to_string(other.line) + "."});
                break;
            }
        table.norms_.push_back(norm);
    }

    if (errors.empty() && table.norms_.empty()) errors.push_back({0, "The file is empty."});
    if (!errors.empty()) return false;
    out = table;
    return true;
}

const Norm* NormTable::find(TestKind kind, int age) const
{
    for (const auto& norm : norms_)
        if (norm.kind == kind && age >= norm.fromAge && age <= norm.toAge) return &norm;
    return nullptr;
}

double resultOf(const Visit& visit, TestKind kind)
{
    if (kind == TestKind::Reaction) return visit.reactionMs;
    if (kind == TestKind::Memory) return visit.memoryDigits;
    return visit.colorWordSeconds;
}

void setResult(Visit& visit, TestKind kind, double value)
{
    if (kind == TestKind::Reaction) visit.reactionMs = (int)value;
    else if (kind == TestKind::Memory) visit.memoryDigits = (int)value;
    else visit.colorWordSeconds = value;
}

Rating rate(const ClinicTest& test, double value, const Norm& norm)
{
    double worseBy = test.lowerIsBetter ? value - norm.average : norm.average - value;
    if (worseBy > WELL_BELOW_SPREADS * norm.spread + TINY) return Rating::WellBelow;
    if (worseBy > BELOW_SPREADS * norm.spread + TINY) return Rating::Below;
    return Rating::Normal;
}

Trend trendBetween(const ClinicTest& test, double older, double newer)
{
    if (!test.lowerIsBetter) {
        if (newer > older) return Trend::Improving;
        return newer < older ? Trend::Worse : Trend::Steady;
    }
    if (newer < older * (1 - TREND_CHANGE) - TINY) return Trend::Improving;
    if (newer > older * (1 + TREND_CHANGE) + TINY) return Trend::Worse;
    return Trend::Steady;
}

int barLength(const ClinicTest& test, double value)
{
    return (int)std::lround(value / test.perHash);
}

std::string ratingText(Rating rating)
{
    if (rating == Rating::WellBelow) return "well below";
    return rating == Rating::Below ? "below" : "normal";
}

std::string trendText(Trend trend)
{
    if (trend == Trend::Improving) return "improving";
    return trend == Trend::Worse ? "worse" : "steady";
}

static std::string decimalText(double value, int places)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(places) << value;
    return os.str();
}

std::string valueText(const ClinicTest& test, double value)
{
    if (test.kind == TestKind::Reaction) return decimalText(value, 0) + " ms";
    if (test.kind == TestKind::ColorWord) return decimalText(value, 1) + " s";
    return decimalText(value, 0) + (value == 1 ? " digit" : " digits");
}

std::string normText(const ClinicTest& test, double value)
{
    if (test.kind == TestKind::Memory) return decimalText(value, 1) + " digits";
    return valueText(test, value);
}

static bool expectedWeak(const ClinicTest& test, const std::vector<std::string>& stoppedAreas)
{
    for (const auto& area : test.weakAreas)
        if (std::find(stoppedAreas.begin(), stoppedAreas.end(), area) != stoppedAreas.end())
            return true;
    return false;
}

std::vector<CheckRow> checkVisits(const std::vector<Visit>& visits,
                                  const std::vector<const Norm*>& norms,
                                  const std::vector<std::string>& stoppedAreas)
{
    std::vector<CheckRow> rows;
    const std::vector<ClinicTest>& tests = clinicTests();
    const Visit& latest = visits.back();

    for (size_t i = 0; i < tests.size(); ++i) {
        CheckRow row;
        row.test = &tests[i];
        row.value = resultOf(latest, tests[i].kind);
        row.rating = rate(tests[i], row.value, *norms[i]);
        row.expectedWeak = expectedWeak(tests[i], stoppedAreas);
        if (visits.size() >= 2) {
            double older = resultOf(visits[visits.size() - 2], tests[i].kind);
            row.hasTrend = true;
            row.trend = trendBetween(tests[i], older, row.value);
        }
        rows.push_back(row);
    }
    return rows;
}
