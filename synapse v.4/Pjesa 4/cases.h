#ifndef CASES_H
#define CASES_H

#include "triage.h"
#include <string>
#include <vector>

struct CaseUpdate
{
    int time = 0;
    std::vector<std::string> addedSymptoms;
    int severity = 0;
    std::string likelyPipe;
};

static const int NO_TIME = -1;
static const std::string TREATMENT_CLOT = "clot removal";
static const std::string TREATMENT_DRUG = "drug";
static const std::string TREATMENT_SCAN = "scan";

// Where the ambulance goes. With a transfer, the main fields describe the last
// leg to the clot removal hospital and the first fields the drug stop before it.
struct CaseRoute
{
    std::string treatment;
    std::string hospital;
    std::vector<std::string> path;
    int driveMinutes = 0;
    int leaves = 0;
    int arrives = 0;
    int drugAt = NO_TIME;
    int clotAt = NO_TIME;
    bool transfer = false;
    std::string firstHospital;
    std::vector<std::string> firstPath;
    int firstDriveMinutes = 0;
    int firstLeaves = 0;
    int firstArrives = 0;
};

struct Case
{
    std::string id;
    std::string name;
    int age = 0;
    std::string city;
    int createdAt = 0;
    int timeNow = 0;                     // the latest time the case was assessed
    int lastSeen = 0;                    // minutes from the start of today
    std::vector<std::string> symptoms;
    Assessment result;
    std::vector<CaseUpdate> history;
    bool routed = false;
    CaseRoute route;
};

// The patient database: one JSON file, rewritten after every change.
class CaseStore
{
public:
    // A missing file is a new, empty database. A damaged one is kept as it
    // is: nothing can be saved over it.
    bool load(const std::string& path);

    bool add(Case& c);                   // gives the case its id
    bool update(const Case& c);

    const std::vector<Case>& all() const { return cases_; }
    const Case* find(const std::string& id) const;
    const std::string& path() const { return path_; }

private:
    bool save() const;

    std::string path_;
    std::vector<Case> cases_;
    int nextId_ = 1;
    bool damaged_ = false;
};

#endif // CASES_H
