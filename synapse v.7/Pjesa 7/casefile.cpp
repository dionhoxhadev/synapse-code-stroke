#include "casefile.h"
#include "atlas.h"

// Keeps the earlier of two treatments. Planned times are used when no actual time exists.
static void consider(FirstTreatment& best, const std::string& kind, int planned, int actual)
{
    bool hasActual = actual != NO_TIME;
    if (!hasActual && planned == NO_TIME) return;

    int minutes = hasActual ? actual : planned;
    if (best.found && best.minutes <= minutes) return;   // an earlier or equal one stays

    best.found = true;
    best.kind = kind;
    best.minutes = minutes;
    best.actual = hasActual;
    best.waitKnown = hasActual && planned != NO_TIME;
    best.waited = best.waitKnown ? actual - planned : 0;
}

FirstTreatment firstTreatment(const Case& c)
{
    int plannedDrug = c.routed ? c.route.drugAt : NO_TIME;
    int plannedClot = c.routed ? c.route.clotAt : NO_TIME;
    int actualDrug = c.stayed ? c.stay.drugAt : NO_TIME;
    int actualClot = c.stayed ? c.stay.clotAt : NO_TIME;

    FirstTreatment best;
    consider(best, TREATMENT_DRUG, plannedDrug, actualDrug);
    consider(best, TREATMENT_CLOT, plannedClot, actualClot);
    return best;
}

std::vector<const Case*> findCases(const std::vector<Case>& cases, const std::string& text)
{
    std::string key = toLower(text);
    std::vector<const Case*> found;
    for (const auto& c : cases)
        if (toLower(c.name).find(key) != std::string::npos ||
            toLower(c.city).find(key) != std::string::npos)
            found.push_back(&c);
    return found;
}
