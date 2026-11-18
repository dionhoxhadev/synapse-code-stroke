#ifndef CITYMAP_H
#define CITYMAP_H

#include "atlas.h"
#include <climits>
#include <string>
#include <unordered_map>
#include <vector>

static const std::string ABILITY_CT = "CT";
static const std::string ABILITY_DRUG = "DRUG";
static const std::string ABILITY_CLOT = "CLOT";
static const int NO_ROAD = INT_MAX;

struct Road
{
    int to;
    int minutes;
};

struct Hospital
{
    std::string name;
    std::string city;
    std::vector<std::string> abilities;
    int line = 0;

    bool can(const std::string& ability) const;
};

// The shortest drive from one city to every other city.
struct DriveTimes
{
    std::vector<int> minutes;     // NO_ROAD when a city cannot be reached
    std::vector<int> previous;    // the city before this one on the way, or -1
};

class CityMap
{
public:
    // Loads roads or hospitals, whichever the file holds. All or nothing:
    // on any error the map stays as it was. loaded describes what was read.
    bool load(const std::string& path, std::string& loaded, std::vector<AtlasError>& errors);

    bool hasRoads() const { return !cities_.empty(); }
    bool hasHospitals() const { return !hospitals_.empty(); }
    const std::vector<std::string>& cities() const { return cities_; }
    const std::vector<Hospital>& hospitals() const { return hospitals_; }
    int cityIndex(const std::string& name) const;      // -1 when it is not on the map

    DriveTimes drivesFrom(int city) const;
    std::vector<std::string> pathTo(const DriveTimes& drives, int city) const;
    void addAbility(int city, const std::string& ability);

private:
    struct Record
    {
        int line;
        std::vector<std::string> fields;
    };

    bool loadRoads(const std::vector<Record>& records, std::string& loaded,
                   std::vector<AtlasError>& errors);
    bool loadHospitals(const std::vector<Record>& records, std::string& loaded,
                       std::vector<AtlasError>& errors);

    std::vector<std::string> cities_;              // first column first, as the file lists them
    std::unordered_map<std::string, int> cityIndex_;   // by lowercase name
    std::vector<std::vector<Road>> roads_;         // per city
    std::vector<Hospital> hospitals_;              // in file order
    int roadCount_ = 0;
    std::string hospitalFile_;
};

#endif // CITYMAP_H
