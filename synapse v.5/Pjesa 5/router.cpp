#include "router.h"
#include <algorithm>

std::vector<HospitalDrive> Router::hospitalsFrom(int city) const
{
    DriveTimes times = map_.drivesFrom(city);
    std::vector<HospitalDrive> out;
    for (const auto& h : map_.hospitals()) {
        int target = map_.cityIndex(h.city);
        if (target == city)
            out.push_back({&h, OWN_CITY_DRIVE, {h.city}});
        else if (times.minutes[target] != NO_ROAD)
            out.push_back({&h, times.minutes[target], map_.pathTo(times, target)});
    }
    std::stable_sort(out.begin(), out.end(),
        [](const HospitalDrive& x, const HospitalDrive& y) { return x.minutes < y.minutes; });
    return out;
}

const HospitalDrive* Router::nearest(const std::vector<HospitalDrive>& drives,
                                     const std::string& ability) const
{
    for (const auto& d : drives)
        if (d.hospital->can(ability)) return &d;
    return nullptr;
}

CaseRoute Router::straightPlan(const Case& c, const HospitalDrive& drive,
                               const std::string& treatment) const
{
    CaseRoute r;
    r.treatment = treatment;
    r.hospital = drive.hospital->name;
    r.path = drive.path;
    r.driveMinutes = drive.minutes;
    r.leaves = c.createdAt;
    r.arrives = r.leaves + drive.minutes;
    if (treatment == TREATMENT_CLOT) r.clotAt = r.arrives + ARRIVAL_TO_CLOT;
    if (treatment != TREATMENT_SCAN && c.result.drugPossible && drive.hospital->can(ABILITY_DRUG) &&
        r.arrives + ARRIVAL_TO_DRUG <= c.result.drugUntil)
        r.drugAt = r.arrives + ARRIVAL_TO_DRUG;
    return r;
}

bool Router::decide(const Case& c, int city, RouteDecision& decision) const
{
    std::vector<HospitalDrive> drives = hospitalsFrom(city);
    if (drives.empty()) {
        decision.failure = "No road leads from " + map_.cities()[city] + " to any hospital.";
        return false;
    }

    const Assessment& a = c.result;
    if (a.clotPossible)
        for (const auto& d : drives)
            if (d.hospital->can(ABILITY_CLOT) &&
                c.createdAt + d.minutes + ARRIVAL_TO_CLOT <= a.clotUntil) {
                decision.route = straightPlan(c, d, TREATMENT_CLOT);
                compareTransfer(c, drives, decision);
                return true;
            }

    if (a.drugPossible)
        for (const auto& d : drives)
            if (d.hospital->can(ABILITY_DRUG) &&
                c.createdAt + d.minutes + ARRIVAL_TO_DRUG <= a.drugUntil) {
                decision.route = straightPlan(c, d, TREATMENT_DRUG);
                return true;
            }

    if (const HospitalDrive* d = nearest(drives, ABILITY_CT)) {
        decision.route = straightPlan(c, *d, TREATMENT_SCAN);
        return true;
    }
    decision.failure = "No road leads from " + map_.cities()[city] + " to a hospital with CT.";
    return false;
}

// Transfer plan: the fastest drug hospital first, then a transfer to clot removal.
void Router::compareTransfer(const Case& c, const std::vector<HospitalDrive>& drives,
                             RouteDecision& decision) const
{
    const Assessment& a = c.result;
    if (!a.drugPossible) return;

    const HospitalDrive* first = nearest(drives, ABILITY_DRUG);
    if (!first || first->hospital->can(ABILITY_CLOT)) return;
    int firstArrives = c.createdAt + first->minutes;
    int drugAt = firstArrives + ARRIVAL_TO_DRUG;
    if (drugAt > a.drugUntil) return;

    std::vector<HospitalDrive> onward = hospitalsFrom(map_.cityIndex(first->hospital->city));
    const HospitalDrive* second = nearest(onward, ABILITY_CLOT);
    if (!second) return;

    CaseRoute b;
    b.treatment = TREATMENT_CLOT;
    b.hospital = second->hospital->name;
    b.path = second->path;
    b.driveMinutes = second->minutes;
    b.leaves = firstArrives + TRANSFER_LEAVES_AFTER;
    b.arrives = b.leaves + second->minutes;
    b.drugAt = drugAt;
    b.clotAt = b.arrives + TRANSFER_ARRIVAL_TO_CLOT;
    if (b.clotAt > a.clotUntil) return;

    b.transfer = true;
    b.firstHospital = first->hospital->name;
    b.firstPath = first->path;
    b.firstDriveMinutes = first->minutes;
    b.firstLeaves = c.createdAt;
    b.firstArrives = firstArrives;

    decision.comparedPlans = true;
    decision.planA = decision.route;
    decision.planB = b;

    const CaseRoute& planA = decision.planA;
    if (planA.drugAt == NO_TIME) {
        decision.route = b;
        decision.reason = "Chosen: Plan B. Only this plan gives the drug in time.";
    } else if (b.clotAt < planA.clotAt) {
        decision.route = b;
        decision.reason = "Chosen: Plan B. It removes the clot " +
                          std::to_string(planA.clotAt - b.clotAt) + " min sooner.";
    } else if (planA.clotAt < b.clotAt) {
        decision.reason = "Chosen: Plan A. It removes the clot " +
                          std::to_string(b.clotAt - planA.clotAt) + " min sooner.";
    } else {
        decision.reason = "Chosen: Plan A. Both plans remove the clot at the same time.";
    }
}
