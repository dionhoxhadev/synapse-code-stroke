#include "citymap.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <queue>
#include <unordered_set>

static const int ROAD_FIELDS = 4;
static const int HOSPITAL_FIELDS = 4;

static bool parseMinutes(const std::string& text, int& minutes)
{
    if (text.empty() || text.size() > 6) return false;
    for (unsigned char c : text)
        if (!std::isdigit(c)) return false;
    minutes = std::stoi(text);
    return minutes > 0;
}

bool Hospital::can(const std::string& ability) const
{
    return std::find(abilities.begin(), abilities.end(), ability) != abilities.end();
}

int CityMap::cityIndex(const std::string& name) const
{
    auto it = cityIndex_.find(toLower(name));
    return it == cityIndex_.end() ? -1 : it->second;
}

bool CityMap::load(const std::string& path, std::string& loaded, std::vector<AtlasError>& errors)
{
    errors.clear();
    std::ifstream f(path);
    if (!f) {
        errors.push_back({0, "File not found: " + path});
        return false;
    }

    std::vector<Record> records;
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (lineNo == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);   // BOM
        line = trim(line);
        if (!line.empty() && line[0] != '#') records.push_back({lineNo, split(line, '|')});
    }
    if (records.empty()) {
        errors.push_back({0, "The file is empty."});
        return false;
    }

    const std::string& kind = records[0].fields[0];
    if (kind == "ROAD") {
        CityMap updated = *this;
        if (!updated.loadRoads(records, loaded, errors)) return false;
        *this = updated;
        return true;
    }
    if (kind == "HOSPITAL") {
        if (!hasRoads()) {
            errors.push_back({0, "Load the roads before the hospitals."});
            return false;
        }
        CityMap updated = *this;
        updated.hospitalFile_ = baseName(path);
        if (!updated.loadHospitals(records, loaded, errors)) return false;
        *this = updated;
        return true;
    }
    errors.push_back({records[0].line, "unknown record type " + inQuotes(kind) +
                                       ". Expected ROAD or HOSPITAL."});
    return false;
}

bool CityMap::loadRoads(const std::vector<Record>& records, std::string& loaded,
                        std::vector<AtlasError>& errors)
{
    struct RoadLine
    {
        std::string first;
        std::string second;
        int minutes;
    };

    std::vector<RoadLine> lines;
    std::unordered_set<std::string> seen;

    for (const auto& r : records) {
        const auto& fields = r.fields;
        if (fields[0] != "ROAD") {
            errors.push_back({r.line, "unknown record type " + inQuotes(fields[0]) +
                                      ". Expected ROAD."});
            continue;
        }
        if ((int)fields.size() != ROAD_FIELDS) {
            errors.push_back({r.line, "ROAD needs " + std::to_string(ROAD_FIELDS) +
                                      " fields, found " + std::to_string(fields.size()) + "."});
            continue;
        }

        const std::string& a = fields[1];
        const std::string& b = fields[2];
        int minutes = 0;
        if (!parseMinutes(fields[3], minutes))
            errors.push_back({r.line, "minutes must be a whole number above 0, found " +
                                      inQuotes(fields[3]) + "."});
        if (a.empty() || b.empty()) {
            errors.push_back({r.line, "a road needs two cities."});
            continue;
        }
        if (toLower(a) == toLower(b)) {
            errors.push_back({r.line, "a road cannot connect " + a + " to itself."});
            continue;
        }
        std::string lowA = toLower(a), lowB = toLower(b);
        if (!seen.insert(std::min(lowA, lowB) + "|" + std::max(lowA, lowB)).second) {
            errors.push_back({r.line, "the road " + a + " - " + b + " is listed twice."});
            continue;
        }
        lines.push_back({a, b, minutes});
    }

    cities_.clear();
    cityIndex_.clear();
    auto addCity = [&](const std::string& name) {
        if (cityIndex(name) >= 0) return;
        cityIndex_[toLower(name)] = (int)cities_.size();
        cities_.push_back(name);
    };
    for (const auto& l : lines) addCity(l.first);
    for (const auto& l : lines) addCity(l.second);

    roads_.assign(cities_.size(), {});
    for (const auto& l : lines) {
        int a = cityIndex(l.first), b = cityIndex(l.second);
        roads_[a].push_back({b, l.minutes});
        roads_[b].push_back({a, l.minutes});
    }
    roadCount_ = (int)lines.size();

    // A closed road must not leave a hospital off the map.
    for (auto& h : hospitals_) {
        int city = cityIndex(h.city);
        if (city < 0)
            errors.push_back({0, hospitalFile_ + ", line " + std::to_string(h.line) + ": city " +
                                 inQuotes(h.city) + " is not on any road."});
    }

    loaded = std::to_string(cities_.size()) + " cities, " + std::to_string(roadCount_) + " roads";
    return errors.empty();
}

bool CityMap::loadHospitals(const std::vector<Record>& records, std::string& loaded,
                            std::vector<AtlasError>& errors)
{
    std::vector<Hospital> list;
    std::unordered_map<std::string, int> firstLine;

    for (const auto& r : records) {
        const auto& fields = r.fields;
        if (fields[0] != "HOSPITAL") {
            errors.push_back({r.line, "unknown record type " + inQuotes(fields[0]) +
                                      ". Expected HOSPITAL."});
            continue;
        }
        if ((int)fields.size() != HOSPITAL_FIELDS) {
            errors.push_back({r.line, "HOSPITAL needs " + std::to_string(HOSPITAL_FIELDS) +
                                      " fields, found " + std::to_string(fields.size()) + "."});
            continue;
        }

        Hospital h;
        h.name = fields[1];
        h.line = r.line;
        if (h.name.empty()) errors.push_back({r.line, "a hospital needs a name."});

        auto dup = firstLine.find(h.name);
        if (dup != firstLine.end()) {
            errors.push_back({r.line, inQuotes(h.name) + " is defined twice (first on line " +
                                      std::to_string(dup->second) + ")."});
            continue;
        }
        firstLine[h.name] = r.line;

        int city = cityIndex(fields[2]);
        if (city < 0)
            errors.push_back({r.line, "city " + inQuotes(fields[2]) + " is not on any road."});
        else
            h.city = cities_[city];

        for (const auto& ability : split(fields[3], ',')) {
            if (ability != ABILITY_CT && ability != ABILITY_DRUG && ability != ABILITY_CLOT)
                errors.push_back({r.line, "unknown ability " + inQuotes(ability) +
                                          ". Expected CT, DRUG or CLOT."});
            else if (!h.can(ability))
                h.abilities.push_back(ability);
        }
        if (h.can(ABILITY_DRUG) && !h.can(ABILITY_CT))
            errors.push_back({r.line, "a hospital that gives the DRUG must also have CT."});
        list.push_back(h);
    }

    if (!errors.empty()) return false;
    hospitals_ = list;
    loaded = std::to_string(hospitals_.size()) + " hospitals";
    return true;
}

// Dijkstra's algorithm: always settle the closest city not yet settled.
DriveTimes CityMap::drivesFrom(int city) const
{
    typedef std::pair<int, int> Entry;     // minutes, city
    DriveTimes d;
    d.minutes.assign(cities_.size(), NO_ROAD);
    d.previous.assign(cities_.size(), -1);
    d.minutes[city] = 0;

    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> todo;
    todo.push({0, city});
    while (!todo.empty()) {
        Entry cur = todo.top();
        todo.pop();
        if (cur.first > d.minutes[cur.second]) continue;     // an older, slower entry
        for (const auto& road : roads_[cur.second]) {
            int arrival = cur.first + road.minutes;
            if (arrival < d.minutes[road.to]) {
                d.minutes[road.to] = arrival;
                d.previous[road.to] = cur.second;
                todo.push({arrival, road.to});
            }
        }
    }
    return d;
}

std::vector<std::string> CityMap::pathTo(const DriveTimes& drives, int city) const
{
    std::vector<std::string> path;
    for (int c = city; c >= 0; c = drives.previous[c]) path.push_back(cities_[c]);
    std::reverse(path.begin(), path.end());
    return path;
}

void CityMap::addAbility(int city, const std::string& ability)
{
    for (auto& h : hospitals_)
        if (h.city == cities_[city] && !h.can(ability)) h.abilities.push_back(ability);
}
