#ifndef ROUTER_H
#define ROUTER_H

#include "cases.h"
#include "citymap.h"
#include <string>
#include <vector>

// Every routing time lives here, in minutes.
static const int OWN_CITY_DRIVE = 10;
static const int ARRIVAL_TO_DRUG = 30;
static const int ARRIVAL_TO_CLOT = 60;
static const int TRANSFER_LEAVES_AFTER = 45;
static const int TRANSFER_ARRIVAL_TO_CLOT = 30;

struct HospitalDrive
{
    const Hospital* hospital;
    int minutes;
    std::vector<std::string> path;
};

struct RouteDecision
{
    CaseRoute route;                 // the plan chosen
    bool comparedPlans = false;      // both plans were possible
    CaseRoute planA;
    CaseRoute planB;
    std::string reason;
    std::string failure;             // why no route exists
};

class Router
{
public:
    explicit Router(const CityMap& map) : map_(map) {}

    // Every hospital that can be reached, fastest first; ties keep file order.
    std::vector<HospitalDrive> hospitalsFrom(int city) const;
    const HospitalDrive* nearest(const std::vector<HospitalDrive>& drives,
                                 const std::string& ability) const;

    bool decide(const Case& c, int city, RouteDecision& decision) const;

private:
    CaseRoute straightPlan(const Case& c, const HospitalDrive& drive,
                           const std::string& treatment) const;
    void compareTransfer(const Case& c, const std::vector<HospitalDrive>& drives,
                         RouteDecision& decision) const;

    const CityMap& map_;
};

#endif // ROUTER_H
