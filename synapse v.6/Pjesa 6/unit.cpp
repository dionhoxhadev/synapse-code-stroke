#include "unit.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

static const int ARRIVAL_FIELDS = 7;
static const int MINUTES_PER_DAY = 24 * 60;
static const std::string NOT_GIVEN = "-";

enum EventRank { ARRIVES, SCAN_STARTS, CLOT_STARTS, DRUG_GIVEN, DRUG_MISSED };

static bool parseSeverity(const std::string& text, int& severity)
{
    if (text.empty() || text.size() > 4) return false;
    for (unsigned char c : text)
        if (!std::isdigit(c)) return false;
    severity = std::stoi(text);
    return true;
}

bool UnitPatient::canGetDrug() const
{
    return !isHead && drugGivenAt.empty() && arrives <= drugLimit;
}

bool UnitPatient::scanLate() const
{
    return isHead && scanAt - arrives > HEAD_SCAN_LIMIT;
}

static void readEvents(std::ifstream& f, Morning& morning, std::vector<AtlasError>& errors)
{
    std::unordered_map<std::string, int> firstLine;
    std::string line;
    int lineNo = 0;

    while (std::getline(f, line)) {
        ++lineNo;
        if (lineNo == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);   // BOM
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto problem = [&](const std::string& text) { errors.push_back({lineNo, text}); };

        std::vector<std::string> fields = split(line, '|');
        if (fields[0] != "ARRIVAL") {
            problem("unknown record type " + inQuotes(fields[0]) + ". Expected ARRIVAL.");
            continue;
        }
        if ((int)fields.size() != ARRIVAL_FIELDS) {
            problem("ARRIVAL needs " + std::to_string(ARRIVAL_FIELDS) + " fields, found " +
                    std::to_string(fields.size()) + ".");
            continue;
        }

        UnitPatient p;
        p.id = fields[2];
        const std::string& type = fields[3];
        if (!parseClock(fields[1], p.arrives))
            problem("invalid time " + inQuotes(fields[1]) + ". Use HH:MM.");
        if (p.id.empty()) problem("a patient needs an id.");
        if (type != "stroke" && type != "head")
            problem("unknown patient type " + inQuotes(type) + ". Expected stroke or head.");

        p.isHead = type == "head";
        if (type == "stroke") {
            if (!parseSeverity(fields[4], p.severity))
                problem("severity must be a whole number, found " + inQuotes(fields[4]) + ".");

            int lastSeen = 0;
            if (!parseClock(fields[5], lastSeen))
                problem("invalid time " + inQuotes(fields[5]) + ". Use HH:MM.");
            if (lastSeen > p.arrives) lastSeen -= MINUTES_PER_DAY;     // it must have been yesterday
            p.drugLimit = drugLimit(lastSeen);

            if (fields[6] != "yes" && fields[6] != "no") problem("big pipe must be yes or no.");
            p.needsClot = fields[6] == "yes" && p.severity >= CLOT_MIN_SEVERITY;
        }

        auto first = firstLine.find(p.id);
        if (first != firstLine.end()) {
            problem("patient " + p.id + " is listed twice (first on line " +
                    std::to_string(first->second) + ").");
            continue;
        }
        firstLine[p.id] = lineNo;
        morning.patients.push_back(p);
    }
}

static void addCases(const CaseStore& cases, Morning& morning)
{
    for (const auto& c : cases.all()) {
        if (!c.routed) {
            morning.noRoute.push_back(c.id);
            continue;
        }
        if (c.route.hospital != UNIT_HOSPITAL) {
            morning.elsewhere.push_back(c.id);
            continue;
        }
        bool alreadyListed = false;
        for (const auto& p : morning.patients)
            if (p.id == c.id) alreadyListed = true;
        if (alreadyListed) {
            morning.duplicates.push_back(c.id);
            continue;
        }

        UnitPatient p;
        p.id = c.id;
        p.fromCase = true;
        p.arrives = c.route.arrives;
        p.severity = c.result.severity;
        p.drugLimit = c.result.drugUntil;
        p.needsClot = c.result.clotPossible;
        if (c.route.transfer) p.drugGivenAt = c.route.firstHospital;
        morning.patients.push_back(p);
        ++morning.fromCases;
    }
}

bool loadMorning(const std::string& path, const CaseStore& cases, Morning& morning,
                 std::vector<AtlasError>& errors)
{
    errors.clear();
    std::ifstream f(path);
    if (!f) {
        errors.push_back({0, "File not found: " + path});
        return false;
    }
    readEvents(f, morning, errors);
    if (!errors.empty()) return false;
    morning.fromFile = (int)morning.patients.size();
    addCases(cases, morning);
    return true;
}

// Only this function knows the difference between the two orders.
bool StrokeUnit::goesFirst(const UnitPatient& x, const UnitPatient& y, int now) const
{
    if (order_ == ScanOrder::Deadline) {
        const int margin = SCAN_MINUTES + SCAN_END_TO_DRUG;
        auto deadline = [&](const UnitPatient& p) {
            if (p.canGetDrug() && now + margin <= p.drugLimit) return p.drugLimit - margin;
            return p.arrives + STANDARD_DEADLINE;
        };
        int dx = deadline(x), dy = deadline(y);
        if (dx != dy) return dx < dy;
        if (x.severity != y.severity) return x.severity > y.severity;
    }
    if (x.arrives != y.arrives) return x.arrives < y.arrives;
    return x.id < y.id;
}

void StrokeUnit::runScanner(std::vector<UnitPatient>& patients) const
{
    std::vector<int> freeAt(scanners_, 0);

    for (size_t done = 0; done < patients.size(); ++done) {
        auto scanner = std::min_element(freeAt.begin(), freeAt.end());

        int firstWaiting = -1;
        for (const auto& p : patients)
            if (p.scanAt == NO_TIME && (firstWaiting < 0 || p.arrives < firstWaiting))
                firstWaiting = p.arrives;
        int now = std::max(*scanner, firstWaiting);

        UnitPatient* next = nullptr;
        for (auto& p : patients)
            if (p.scanAt == NO_TIME && p.arrives <= now && (!next || goesFirst(p, *next, now)))
                next = &p;

        next->scanAt = now;
        *scanner = now + SCAN_MINUTES;

        int drugTime = now + SCAN_MINUTES + SCAN_END_TO_DRUG;
        if (next->canGetDrug()) {
            if (drugTime <= next->drugLimit) next->drugAt = drugTime;
            else next->missedDrug = true;
        }
    }
}

void StrokeUnit::runRoom(std::vector<UnitPatient>& patients) const
{
    std::vector<UnitPatient*> line;
    for (auto& p : patients)
        if (p.needsClot) line.push_back(&p);
    std::stable_sort(line.begin(), line.end(), [](const UnitPatient* x, const UnitPatient* y) {
        return x->scanAt < y->scanAt;
    });

    int freeAt = 0;
    for (UnitPatient* p : line) {
        int ready = p->scanAt + SCAN_MINUTES + SCAN_END_TO_ROOM;
        p->clotAt = std::max(ready, freeAt);
        freeAt = p->clotAt + CLOT_MINUTES;
    }
}

UnitReport StrokeUnit::run(const std::vector<UnitPatient>& patients) const
{
    UnitReport r;
    r.patients = patients;
    std::stable_sort(r.patients.begin(), r.patients.end(),
        [](const UnitPatient& x, const UnitPatient& y) {
            if (x.arrives != y.arrives) return x.arrives < y.arrives;
            return x.id < y.id;
        });

    runScanner(r.patients);
    runRoom(r.patients);

    int total = 0;
    for (const auto& p : r.patients) {
        r.events.push_back({p.arrives, ARRIVES, p.id, "arrives"});
        r.events.push_back({p.scanAt, SCAN_STARTS, p.id, "scan starts"});
        if (p.clotAt != NO_TIME) r.events.push_back({p.clotAt, CLOT_STARTS, p.id, "clot removal starts"});
        if (p.drugAt != NO_TIME)
            r.events.push_back({p.drugAt, DRUG_GIVEN, p.id, "drug given (" +
                                std::to_string(p.doorToDrug()) + " min after arrival)"});
        if (p.missedDrug)
            r.events.push_back({p.drugLimit, DRUG_MISSED, p.id,
                                "drug limit passed before the drug could be given"});

        if (p.canGetDrug()) ++r.couldGetDrug;
        if (p.drugAt != NO_TIME) {
            ++r.drugGiven;
            total += p.doorToDrug();
            if (p.doorToDrug() > SLOW_DOOR_TO_DRUG) r.slow.push_back(p.id);
        }
        if (p.missedDrug) r.missed.push_back(p.id);
        if (p.scanLate()) r.lateHeads.push_back(p.id);
    }
    if (r.drugGiven > 0) r.averageDoorToDrug = (total * 2 + r.drugGiven) / (r.drugGiven * 2);

    std::stable_sort(r.events.begin(), r.events.end(), [](const UnitEvent& x, const UnitEvent& y) {
        if (x.time != y.time) return x.time < y.time;
        if (x.rank != y.rank) return x.rank < y.rank;
        return x.id < y.id;
    });
    return r;
}
