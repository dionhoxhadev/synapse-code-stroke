#ifndef CLINIC_H
#define CLINIC_H

#include "cases.h"
#include <string>
#include <vector>

// Every clinic rule number lives here.
static const int MAX_MEMORY_DIGITS = 9;
static const int MAX_WHOLE = 999999999;        // the longest number parseWhole reads
static const double TREND_CHANGE = 0.05;       // 5%
static const double BELOW_SPREADS = 1;
static const double WELL_BELOW_SPREADS = 2;
static const double TINY = 0.000000001;        // a result exactly on a limit stays on it

enum class TestKind { Reaction, Memory, ColorWord };
enum class Rating { Normal, Below, WellBelow };
enum class Trend { Improving, Steady, Worse };

// What is different between the three tests. The rules below use only this table.
struct ClinicTest
{
    TestKind kind;
    std::string code;                     // as written in norms.txt
    std::string name;                     // "Colour words"
    std::string longName;                 // "Colour words", "Reaction time"
    bool lowerIsBetter;
    double perHash;                       // what one # stands for in a chart
    std::vector<std::string> weakAreas;   // expected to be weak if one of these stopped
};

const std::vector<ClinicTest>& clinicTests();
const ClinicTest* findTest(const std::string& code);

struct Norm
{
    TestKind kind;
    int fromAge;
    int toAge;
    double average;
    double spread;
    int line;
};

class NormTable
{
public:
    // All or nothing, like Atlas::load.
    static bool load(const std::string& path, NormTable& out, std::vector<AtlasError>& errors);

    const Norm* find(TestKind kind, int age) const;
    size_t size() const { return norms_.size(); }

private:
    std::vector<Norm> norms_;
};

// Strict number reading: only digits, and for a decimal one point.
bool parseWhole(const std::string& text, int& value);
bool parseDecimal(const std::string& text, double& value);
double oneDecimal(double value);

double resultOf(const Visit& visit, TestKind kind);
void setResult(Visit& visit, TestKind kind, double value);

Rating rate(const ClinicTest& test, double value, const Norm& norm);
Trend trendBetween(const ClinicTest& test, double older, double newer);
int barLength(const ClinicTest& test, double value);

std::string ratingText(Rating rating);         // "well below"
std::string trendText(Trend trend);            // "improving"
std::string valueText(const ClinicTest& test, double value);   // "540 ms", "4 digits"
std::string normText(const ClinicTest& test, double value);    // "390 ms", "6.0 digits"

// One line of the clinic check, for the latest visit.
struct CheckRow
{
    const ClinicTest* test = nullptr;
    double value = 0;
    Rating rating = Rating::Normal;
    bool hasTrend = false;
    Trend trend = Trend::Steady;
    bool expectedWeak = false;

    bool unexplained() const { return rating != Rating::Normal && !expectedWeak; }
    bool gettingWorse() const { return hasTrend && trend == Trend::Worse; }
};

// norms holds one Norm per clinic test, in the same order as clinicTests().
std::vector<CheckRow> checkVisits(const std::vector<Visit>& visits,
                                  const std::vector<const Norm*>& norms,
                                  const std::vector<std::string>& stoppedAreas);

#endif // CLINIC_H
