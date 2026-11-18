#include "triage.h"
#include "lesion.h"
#include <cctype>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <unordered_set>

static const int MINUTES_PER_DAY = 24 * 60;
static const int SYMPTOM_FIELDS = 4;

bool parseClock(const std::string& text, int& minutes)
{
    if (text.size() != 5 || text[2] != ':') return false;
    for (int i : {0, 1, 3, 4})
        if (!std::isdigit((unsigned char)text[i])) return false;
    int hours = (text[0] - '0') * 10 + (text[1] - '0');
    int mins = (text[3] - '0') * 10 + (text[4] - '0');
    if (hours > 23 || mins > 59) return false;
    minutes = hours * 60 + mins;
    return true;
}

int lastSeenMinutes(int clock, bool yesterday)
{
    return yesterday ? clock - MINUTES_PER_DAY : clock;
}

std::string clockText(int minutes)
{
    int day = minutes < 0 ? -1 : minutes / MINUTES_PER_DAY;
    int clock = minutes - day * MINUTES_PER_DAY;
    std::ostringstream os;
    os << std::setfill('0') << std::setw(2) << clock / 60 << ':'
       << std::setw(2) << clock % 60;
    if (day < 0) os << " yesterday";
    if (day > 0) os << " tomorrow";
    return os.str();
}

std::string durationText(int minutes)
{
    std::ostringstream os;
    os << minutes / 60 << " h " << std::setfill('0') << std::setw(2) << minutes % 60 << " min";
    return os.str();
}

const Symptom* SymptomList::find(const std::string& code) const
{
    auto it = index_.find(code);
    return it == index_.end() ? nullptr : &symptoms_[it->second];
}

static bool parsePoints(const std::string& text, int& points)
{
    if (text.empty() || text.size() > 4) return false;
    for (unsigned char c : text)
        if (!std::isdigit(c)) return false;
    points = std::stoi(text);
    return true;
}

bool SymptomList::load(const std::string& path, SymptomList& out, std::vector<AtlasError>& errors)
{
    errors.clear();
    std::ifstream f(path);
    if (!f) {
        errors.push_back({0, "File not found: " + path});
        return false;
    }

    SymptomList list;
    std::unordered_map<std::string, int> firstLine;
    int lineNo = 0, records = 0;
    std::string line;

    while (std::getline(f, line)) {
        ++lineNo;
        if (lineNo == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);   // BOM
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        ++records;

        std::vector<std::string> fields = split(line, '|');
        if (fields[0] != "SYMPTOM") {
            errors.push_back({lineNo, "unknown record type " + inQuotes(fields[0]) +
                                      ". Expected SYMPTOM."});
            continue;
        }
        if ((int)fields.size() != SYMPTOM_FIELDS) {
            errors.push_back({lineNo, "SYMPTOM needs " + std::to_string(SYMPTOM_FIELDS) +
                                      " fields, found " + std::to_string(fields.size()) + "."});
            continue;
        }

        Symptom s;
        s.code = fields[1];
        s.description = fields[3];
        if (!validName(s.code))
            errors.push_back({lineNo, "invalid code " + inQuotes(s.code) +
                                      ". Codes use lowercase letters, digits and hyphens."});
        if (!parsePoints(fields[2], s.points))
            errors.push_back({lineNo, "points must be a whole number, found " +
                                      inQuotes(fields[2]) + "."});
        if (s.description.empty())
            errors.push_back({lineNo, "symptom " + inQuotes(s.code) + " has no description."});

        auto dup = firstLine.find(s.code);
        if (dup != firstLine.end()) {
            errors.push_back({lineNo, inQuotes(s.code) + " is defined twice (first on line " +
                                      std::to_string(dup->second) + ")."});
            continue;
        }
        firstLine[s.code] = lineNo;
        list.index_[s.code] = (int)list.symptoms_.size();
        list.symptoms_.push_back(s);
    }

    if (records == 0) errors.push_back({0, "The file is empty."});
    if (!errors.empty()) return false;
    out = list;
    return true;
}

std::string bandFor(int severity)
{
    if (severity >= SEVERE_FROM) return "severe";
    if (severity >= MODERATE_FROM) return "moderate";
    return "minor";
}

int drugLimit(int lastSeen)
{
    return lastSeen + DRUG_WINDOW;
}

int clotLimit(int lastSeen)
{
    return lastSeen + CLOT_WINDOW;
}

bool clotAllowed(int now, int limit, bool bigPipe, int severity)
{
    return now <= limit && bigPipe && severity >= CLOT_MIN_SEVERITY;
}

Assessment TriageEngine::assess(const std::vector<std::string>& symptoms,
                                int lastSeen, int now) const
{
    Assessment a;
    for (const auto& code : symptoms)
        if (const Symptom* s = symptoms_.find(code)) a.severity += s->points;
    a.band = bandFor(a.severity);

    matchPipes(symptoms, a);

    a.drugUntil = drugLimit(lastSeen);
    a.drugPossible = now <= a.drugUntil;
    a.clotUntil = clotLimit(lastSeen);
    a.clotPossible = clotAllowed(now, a.clotUntil, a.bigPipe, a.severity);
    return a;
}

// Blocks every pipe through the lesion engine and compares what it would
// cause with what the patient shows.
void TriageEngine::matchPipes(const std::vector<std::string>& symptoms, Assessment& result) const
{
    std::unordered_set<std::string> patient(symptoms.begin(), symptoms.end());
    LesionEngine lesion(atlas_);
    size_t fewest = 0;

    for (const auto& p : atlas_.pipes()) {
        if (p.isHeart()) continue;
        LesionResult r = lesion.block({p.name});
        std::unordered_set<std::string> caused(r.symptoms.begin(), r.symptoms.end());
        if (caused != patient) continue;

        // strictly fewer, so a pipe still tied keeps its place in the atlas
        if (result.likelyPipe == UNKNOWN_PIPE || r.noBlood.size() < fewest) {
            result.likelyPipe = p.name;
            result.bigPipe = p.size == "big";
            fewest = r.noBlood.size();
        }
    }
}
