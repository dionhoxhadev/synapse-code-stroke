#ifndef CASEFILE_H
#define CASEFILE_H

#include "cases.h"
#include <string>
#include <vector>

// Every case-file rule number lives here.
static const long BRAIN_CELLS_PER_MINUTE = 1900000;   // published average, large stroke

// The first treatment a patient got, or is planned to get.
struct FirstTreatment
{
    bool found = false;
    std::string kind;              // TREATMENT_DRUG or TREATMENT_CLOT
    int minutes = NO_TIME;         // actual time if recorded, else the planned one
    bool actual = false;
    bool waitKnown = false;        // true when both actual and planned times exist
    int waited = 0;                // actual minus planned, in minutes
};

// Works out the earliest drug or clot removal. Drug wins a tie.
FirstTreatment firstTreatment(const Case& c);

// Cases whose name or city contains the text, ignoring upper and lower case.
std::vector<const Case*> findCases(const std::vector<Case>& cases, const std::string& text);

#endif // CASEFILE_H
