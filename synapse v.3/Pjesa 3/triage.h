#ifndef TRIAGE_H
#define TRIAGE_H

#include "atlas.h"
#include <string>
#include <unordered_map>
#include <vector>

// Every triage rule number lives here.
static const int MODERATE_FROM = 5;
static const int SEVERE_FROM = 10;
static const int DRUG_WINDOW = 4 * 60 + 30;       // minutes after last seen well
static const int CLOT_WINDOW = 24 * 60;
static const int CLOT_MIN_SEVERITY = 5;
static const std::string UNKNOWN_PIPE = "unknown";

// A time is the number of minutes from the start of today. Yesterday is
// negative and tomorrow is 1440 or more, so times compare and add as numbers.
bool parseClock(const std::string& text, int& minutes);
int lastSeenMinutes(int clock, bool yesterday);
std::string clockText(int minutes);          // "07:30", "07:30 tomorrow"
std::string durationText(int minutes);       // "3 h 50 min"

struct Symptom
{
    std::string code;
    int points = 0;
    std::string description;
};

class SymptomList
{
public:
    // All or nothing, like Atlas::load.
    static bool load(const std::string& path, SymptomList& out, std::vector<AtlasError>& errors);

    const std::vector<Symptom>& all() const { return symptoms_; }
    const Symptom* find(const std::string& code) const;

private:
    std::vector<Symptom> symptoms_;      // in file order
    std::unordered_map<std::string, int> index_;
};

struct Assessment
{
    int severity = 0;
    std::string band;
    std::string likelyPipe = UNKNOWN_PIPE;
    bool bigPipe = false;
    int drugUntil = 0;
    bool drugPossible = false;
    int clotUntil = 0;
    bool clotPossible = false;
};

class TriageEngine
{
public:
    TriageEngine(const Atlas& atlas, const SymptomList& symptoms)
        : atlas_(atlas), symptoms_(symptoms) {}

    Assessment assess(const std::vector<std::string>& symptoms, int lastSeen, int now) const;

private:
    void matchPipes(const std::vector<std::string>& symptoms, Assessment& result) const;

    const Atlas& atlas_;
    const SymptomList& symptoms_;
};

std::string bandFor(int severity);
int drugLimit(int lastSeen);
int clotLimit(int lastSeen);
bool clotAllowed(int now, int limit, bool bigPipe, int severity);

#endif // TRIAGE_H
