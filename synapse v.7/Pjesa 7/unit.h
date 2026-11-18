#ifndef UNIT_H
#define UNIT_H

#include "cases.h"
#include <string>
#include <vector>

static const std::string UNIT_HOSPITAL = "Qendra Neuro Tirana";

// Every stroke unit time lives here, in minutes.
static const int SCAN_MINUTES = 15;
static const int SCAN_END_TO_DRUG = 15;
static const int SCAN_END_TO_ROOM = 45;
static const int CLOT_MINUTES = 60;
static const int SLOW_DOOR_TO_DRUG = 60;
static const int HEAD_SCAN_LIMIT = 60;
static const int STANDARD_DEADLINE = 60;     // after arrival, without a drug limit to protect

enum class ScanOrder { FirstCome, Deadline };

struct UnitPatient
{
    std::string id;
    bool isHead = false;
    int arrives = 0;
    int severity = 0;
    int drugLimit = 0;
    bool needsClot = false;
    bool fromCase = false;
    std::string drugGivenAt;     // the hospital that already gave it, for a transfer

    int scanAt = NO_TIME;
    int drugAt = NO_TIME;
    int clotAt = NO_TIME;
    bool missedDrug = false;

    bool canGetDrug() const;
    bool scanLate() const;
    int doorToDrug() const { return drugAt - arrives; }
};

struct UnitEvent
{
    int time;
    int rank;                    // the order of events inside one minute
    std::string id;
    std::string text;
};

struct UnitReport
{
    std::vector<UnitPatient> patients;     // by arrival
    std::vector<UnitEvent> events;         // in time order
    int couldGetDrug = 0;
    int drugGiven = 0;
    int averageDoorToDrug = 0;
    std::vector<std::string> slow;
    std::vector<std::string> missed;
    std::vector<std::string> lateHeads;
};

// Everyone expected at the hospital this morning.
struct Morning
{
    std::vector<UnitPatient> patients;
    int fromFile = 0;
    int fromCases = 0;
    std::vector<std::string> elsewhere;    // routed to another hospital
    std::vector<std::string> noRoute;
    std::vector<std::string> duplicates;   // in the file and in the database
};

bool loadMorning(const std::string& path, const CaseStore& cases, Morning& morning,
                 std::vector<AtlasError>& errors);

class StrokeUnit
{
public:
    StrokeUnit(int scanners, ScanOrder order) : scanners_(scanners), order_(order) {}

    UnitReport run(const std::vector<UnitPatient>& patients) const;

private:
    bool goesFirst(const UnitPatient& x, const UnitPatient& y, int now) const;
    void runScanner(std::vector<UnitPatient>& patients) const;
    void runRoom(std::vector<UnitPatient>& patients) const;

    int scanners_;
    ScanOrder order_;
};

#endif // UNIT_H
