// Synapse: command-line engine of the stroke command center.
#include "atlas.h"
#include "casefile.h"
#include "cases.h"
#include "citymap.h"
#include "lesion.h"
#include "router.h"
#include "triage.h"
#include "unit.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

static const std::string DEFAULT_ATLAS = "data/atlas.txt";
static const std::string DEFAULT_SYMPTOMS = "data/symptoms.txt";
static const std::string CASES_FILE = "data/cases.json";
static const std::string DEFAULT_ROADS = "data/roads.txt";
static const std::string DEFAULT_HOSPITALS = "data/hospitals.txt";

static std::string join(const std::vector<std::string>& items, const std::string& ifEmpty)
{
    if (items.empty()) return ifEmpty;
    std::string out = items[0];
    for (size_t i = 1; i < items.size(); ++i) out += ", " + items[i];
    return out;
}

static std::string counted(size_t n, const std::string& word)
{
    return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
}

static void printField(const std::string& label, const std::string& value)
{
    std::cout << std::left << std::setw(15) << (label + ":") << value << "\n";
}

static void printCaseField(const std::string& label, const std::string& value)
{
    std::cout << std::left << std::setw(22) << (label + ":") << value << "\n";
}

static std::string upper(std::string s)
{
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

static std::string pipeText(const Assessment& a)
{
    if (a.likelyPipe == UNKNOWN_PIPE) return UNKNOWN_PIPE;
    return a.likelyPipe + (a.bigPipe ? " (big)" : " (small)");
}

static std::string likelyPipeLine(const Assessment& a)
{
    if (a.likelyPipe == UNKNOWN_PIPE)
        return UNKNOWN_PIPE + ", no single pipe matches these symptoms exactly";
    return pipeText(a);
}

static std::string drugLine(const Assessment& a, int now)
{
    if (!a.drugPossible)
        return "no, the time limit passed at " + clockText(a.drugUntil);
    return "yes, until " + clockText(a.drugUntil) +
           " (" + durationText(a.drugUntil - now) + " left)";
}

static std::string clotLine(const Assessment& a, int now)
{
    if (a.clotPossible) return "yes, until " + clockText(a.clotUntil);
    if (now > a.clotUntil) return "no, the time limit passed at " + clockText(a.clotUntil);
    if (a.likelyPipe == UNKNOWN_PIPE) return "no, the blocked pipe is unknown";
    if (!a.bigPipe) return "no, the blocked pipe is too small";
    return "no, the severity is below " + std::to_string(CLOT_MIN_SEVERITY);
}

static std::string pathText(const std::vector<std::string>& path)
{
    if (path.size() == 1) return "inside " + path[0];
    std::string out = path[0];
    for (size_t i = 1; i < path.size(); ++i) out += " -> " + path[i];
    return out;
}

static std::string timeOrDash(int minutes)
{
    return minutes == NO_TIME ? "-" : clockText(minutes);
}

static std::string planSummary(const CaseRoute& r)
{
    return "Arrives " + clockText(r.arrives) + ", drug " +
           (r.drugAt == NO_TIME ? "too late" : clockText(r.drugAt)) +
           ", clot removal " + clockText(r.clotAt);
}

// Everything after the current read position, without the outer spaces.
static std::string restOf(std::istream& in)
{
    std::string rest;
    std::getline(in, rest);
    size_t first = rest.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    return rest.substr(first, rest.find_last_not_of(" \t") - first + 1);
}

// 361000000 -> "361 million", 1900000 -> "1.9 million".
static std::string millionsText(long value)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << value / 1000000.0;
    std::string text = os.str();
    if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0)
        text.erase(text.size() - 2);
    return text + " million";
}

// One line of the case text: a clock time, then what happened.
static std::string timeRow(int minutes, const std::string& text)
{
    std::ostringstream os;
    os << std::left << std::setw(16) << clockText(minutes) << text << "\n";
    return os.str();
}

// One row of the hospital table. Waited is shown only when both times exist.
static std::string stageRow(const std::string& label, int planned, int actual)
{
    std::ostringstream os;
    os << std::left << std::setw(17) << label << std::setw(10) << timeOrDash(planned)
       << std::setw(10) << timeOrDash(actual);
    if (planned != NO_TIME && actual != NO_TIME) os << (actual - planned) << " min";
    os << "\n";
    return os.str();
}

class Shell
{
public:
    void run();

private:
    void execute(const std::string& line);
    void atlasCommand(std::istream& args);
    void loadAtlas(const std::string& path);
    void listPipes() const;
    void listAreas() const;
    void show(const std::string& name) const;
    void find(const std::string& word) const;
    void path(const std::string& name) const;
    void lesionCommand(std::istream& args);
    bool parsePipes(const std::string& text, std::vector<std::string>& pipes) const;
    void lesionBlock(const std::vector<std::string>& pipes) const;
    void lesionTree(const std::vector<std::string>& pipes) const;
    void lesionRisk(const std::string& name) const;
    void triageCommand(std::istream& args);
    void loadSymptoms(const std::string& path);
    void loadCases();
    bool triageReady() const;
    void listSymptoms() const;
    void triageNew();
    void triageList() const;
    void triageShow(const std::string& id) const;
    void triageUpdate(const std::string& id);
    void triageUrgent();
    bool ask(const std::string& question, std::string& answer) const;
    bool askNotEmpty(const std::string& question, const std::string& what, std::string& answer) const;
    bool askAge(int& age) const;
    bool askClock(const std::string& question, int& minutes) const;
    bool askLastSeen(int now, int& lastSeen) const;
    bool askSymptoms(const std::string& question, std::vector<std::string>& codes) const;
    void mapCommand(std::istream& args);
    void loadMap(const std::string& path);
    bool mapReady() const;
    void routeCommand(std::istream& args);
    void routeFrom(const std::string& city) const;
    void routeCase(const std::string& id);
    void routeWhatIf(const std::string& text) const;
    void printDecision(const Case& c, const RouteDecision& d) const;
    void unitCommand(std::istream& args);
    bool readMorningOptions(std::istream& args, std::string& file, ScanOrder& order,
                            int& scanners) const;
    bool loadMorningFile(const std::string& file, Morning& morning) const;
    void unitRun(const std::string& file, ScanOrder order, int scanners);
    void unitCompare(const std::string& file, ScanOrder order) const;
    void printReport(const UnitReport& report) const;
    void saveStays(const UnitReport& report);
    void caseCommand(std::istream& args);
    std::string caseText(const Case& c) const;
    void caseShow(const std::string& id) const;
    void caseFind(const std::string& text) const;
    void caseCost(const std::string& id) const;
    void help() const;

    Atlas atlas_;
    std::string atlasPath_;   // empty until an atlas is loaded
    SymptomList symptoms_;
    std::string symptomsPath_;   // empty until a list is loaded
    CaseStore cases_;
    CityMap map_;
};

void Shell::run()
{
    std::cout << "Synapse 1.0 - type \"help\" to see all commands\n";
    loadAtlas(DEFAULT_ATLAS);
    loadSymptoms(DEFAULT_SYMPTOMS);
    loadCases();
    loadMap(DEFAULT_ROADS);
    loadMap(DEFAULT_HOSPITALS);

    std::string line;
    while (true) {
        std::cout << "\nsynapse> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        std::istringstream first(line);
        std::string command;
        first >> command;
        if (command == "exit" || command == "quit") break;
        execute(line);
    }
}

void Shell::execute(const std::string& line)
{
    std::istringstream args(line);
    std::string command;
    if (!(args >> command)) return;
    if (command == "help") help();
    else if (command == "atlas") atlasCommand(args);
    else if (command == "lesion") lesionCommand(args);
    else if (command == "triage") triageCommand(args);
    else if (command == "map") mapCommand(args);
    else if (command == "route") routeCommand(args);
    else if (command == "case") caseCommand(args);
    else if (command == "unit") unitCommand(args);
    else std::cout << "ERROR: Unknown command \"" << command << "\". Type \"help\".\n";
}

void Shell::atlasCommand(std::istream& args)
{
    std::string sub;
    args >> sub;
    std::string rest = restOf(args);

    if (sub == "load") {
        if (rest.empty()) std::cout << "ERROR: Missing path. Usage: atlas load <path>\n";
        else loadAtlas(rest);
        return;
    }
    if (sub != "pipes" && sub != "areas" && sub != "show" && sub != "find" &&
        sub != "tree" && sub != "path") {
        std::cout << "ERROR: Unknown atlas command. Type \"help\".\n";
        return;
    }
    if (atlasPath_.empty()) {
        std::cout << "ERROR: No atlas is loaded. Use: atlas load <path>\n";
        return;
    }

    if (sub == "pipes") listPipes();
    else if (sub == "areas") listAreas();
    else if (sub == "tree") atlas_.printTree(std::cout);
    else if (rest.empty() && sub == "show") std::cout << "ERROR: Missing name. Usage: atlas show <name>\n";
    else if (rest.empty() && sub == "find") std::cout << "ERROR: Missing word. Usage: atlas find <word>\n";
    else if (rest.empty()) std::cout << "ERROR: Missing name. Usage: atlas path <area>\n";
    else if (sub == "show") show(toLower(rest));
    else if (sub == "find") find(rest);
    else path(toLower(rest));
}

void Shell::loadAtlas(const std::string& path)
{
    Atlas loaded;
    std::vector<AtlasError> errors;
    if (Atlas::load(path, loaded, errors)) {
        atlas_ = loaded;
        atlasPath_ = path;
        std::cout << "SUCCESS: Atlas loaded from " << path << ": "
                  << counted(atlas_.pipes().size(), "pipe") << ", "
                  << counted(atlas_.areas().size(), "area");
        if (atlas_.bridgeCount() > 0)
            std::cout << ", " << counted(atlas_.bridgeCount(), "bridge");
        std::cout << ".\n";
        return;
    }
    for (const auto& e : errors) {
        std::cout << "ERROR: ";
        if (e.line > 0) std::cout << path << ", line " << e.line << ": ";
        std::cout << e.message << "\n";
    }
    std::cout << "Atlas not loaded. ";
    if (atlasPath_.empty()) std::cout << "No atlas in use.\n";
    else std::cout << "Still using " << atlasPath_ << ".\n";
}

void Shell::listPipes() const
{
    size_t width = 4;
    for (const auto& p : atlas_.pipes()) width = std::max(width, p.name.size());
    std::cout << std::left << std::setw(width + 2) << "NAME"
              << std::setw(7) << "SIZE" << "COMES FROM\n";
    for (const auto& p : atlas_.pipes())
        std::cout << std::setw(width + 2) << p.name << std::setw(7) << p.size
                  << join(p.comesFrom, "-") << "\n";
    std::cout << counted(atlas_.pipes().size(), "pipe") << "\n";
}

void Shell::listAreas() const
{
    size_t width = 4;
    for (const auto& a : atlas_.areas()) width = std::max(width, a.name.size());
    std::cout << std::left << std::setw(width + 2) << "NAME" << "FED BY\n";
    for (const auto& a : atlas_.areas())
        std::cout << std::setw(width + 2) << a.name << join(a.fedBy, "-") << "\n";
    std::cout << counted(atlas_.areas().size(), "area") << "\n";
}

void Shell::show(const std::string& name) const
{
    if (const Pipe* p = atlas_.findPipe(name)) {
        printField("Pipe", p->name);
        printField("Size", p->size);
        printField("Comes from", join(p->comesFrom, "-"));
        printField("Branches into", join(p->branches, "none"));
        printField("Feeds areas", join(p->feeds, "none directly"));
        if (!p->bridgedTo.empty()) printField("Bridged to", join(p->bridgedTo, "none"));
    } else if (const Area* a = atlas_.findArea(name)) {
        printField("Area", a->name);
        printField("What it does", a->description);
        printField("Fed by", join(a->fedBy, "-"));
        printField("If it stops", join(a->symptoms, "-"));
    } else {
        std::cout << "ERROR: No pipe or area called \"" << name << "\".\n";
    }
}

void Shell::find(const std::string& word) const
{
    std::string lower = toLower(word);
    std::vector<const Area*> found = atlas_.areasMatching(lower);
    if (found.empty()) {
        std::cout << "No areas match \"" << lower << "\".\n";
        return;
    }
    size_t width = 0;
    for (const Area* a : found) width = std::max(width, a->name.size());
    std::cout << counted(found.size(), "area") << (found.size() == 1 ? " matches" : " match")
              << " \"" << lower << "\":\n";
    for (const Area* a : found)
        std::cout << "  " << std::left << std::setw(width + 2) << a->name
                  << a->description << "\n";
}

void Shell::path(const std::string& name) const
{
    const Area* area = atlas_.findArea(name);
    if (!area) {
        std::cout << "ERROR: No area called \"" << name << "\".\n";
        return;
    }
    std::vector<std::vector<std::string>> routes = atlas_.routesTo(*area);
    std::cout << counted(routes.size(), "route") << " from the heart to " << name << ":\n";
    for (const auto& route : routes) {
        std::cout << " ";
        for (size_t i = 0; i < route.size(); ++i)
            std::cout << (i ? " > " : " ") << route[i];
        std::cout << "\n";
    }
}

void Shell::lesionCommand(std::istream& args)
{
    std::string sub;
    args >> sub;
    std::string rest = toLower(restOf(args));

    if (sub != "block" && sub != "risk" && sub != "tree") {
        std::cout << "ERROR: Unknown lesion command. Type \"help\".\n";
        return;
    }
    if (atlasPath_.empty()) {
        std::cout << "ERROR: No atlas is loaded. Use: atlas load <path>\n";
        return;
    }
    if (sub == "risk") {
        lesionRisk(rest);
        return;
    }

    if (rest.find_first_not_of(", \t") == std::string::npos) {
        std::cout << "ERROR: Missing pipe. Usage: lesion " << sub << " <pipe>\n";
        return;
    }
    std::vector<std::string> pipes;
    if (!parsePipes(rest, pipes)) return;
    if (sub == "block") lesionBlock(pipes);
    else lesionTree(pipes);
}

// Reads a comma separated list of pipes. Prints why a name cannot be blocked.
bool Shell::parsePipes(const std::string& text, std::vector<std::string>& pipes) const
{
    std::istringstream list(text);
    std::string item;
    while (std::getline(list, item, ',')) {
        std::istringstream cleaned(item);
        std::string name = restOf(cleaned);
        if (name.empty()) continue;

        const Pipe* p = atlas_.findPipe(name);
        if (!p) {
            if (atlas_.findArea(name))
                std::cout << "ERROR: \"" << name << "\" is an area, not a pipe. Only pipes can be blocked.\n";
            else
                std::cout << "ERROR: No pipe called \"" << name << "\".\n";
            return false;
        }
        if (p->isHeart()) {
            std::cout << "ERROR: The heart cannot be blocked.\n";
            return false;
        }
        if (std::find(pipes.begin(), pipes.end(), name) == pipes.end()) pipes.push_back(name);
    }
    return true;
}

void Shell::lesionBlock(const std::vector<std::string>& pipes) const
{
    LesionResult r = LesionEngine(atlas_).block(pipes);

    if (r.blocked.size() == 1)
        std::cout << "Blocked pipe: " << r.blocked[0]->name << " (" << r.blocked[0]->size << ")\n";
    else
        std::cout << "Blocked pipes: " << join(pipes, "none") << "\n";

    std::vector<std::string> names;
    for (const Pipe* p : r.noBlood) names.push_back(p->name);
    std::cout << "Pipes without blood (" << names.size() << "): " << join(names, "none") << "\n";

    std::cout << "Areas that stop working (" << r.stopped.size() << "):";
    if (r.stopped.empty()) std::cout << " none";
    std::cout << "\n";
    size_t width = 0;
    for (const Area* a : r.stopped) width = std::max(width, a->name.size());
    for (const Area* a : r.stopped)
        std::cout << "  " << std::left << std::setw(width + 2) << a->name
                  << a->description << "\n";

    if (!r.symptoms.empty())
        std::cout << "Expected symptoms (" << r.symptoms.size() << "): "
                  << join(r.symptoms, "none") << "\n";

    std::vector<std::string> saved;
    for (const auto& s : r.saved) saved.push_back(s.pipe->name + " (from " + s.from + ")");
    if (!saved.empty())
        std::cout << "Saved by a bridge: " << join(saved, "none") << "\n";
}

void Shell::lesionTree(const std::vector<std::string>& pipes) const
{
    LesionResult r = LesionEngine(atlas_).block(pipes);
    std::unordered_map<std::string, std::string> marks;
    for (const Pipe* p : r.noBlood) marks[p->name] = "no blood";
    for (const Pipe* p : r.blocked) marks[p->name] = "BLOCKED";
    for (const Area* a : r.stopped) marks[a->name] = "stops working";
    for (const auto& s : r.saved) marks[s.pipe->name] = "saved by a bridge";
    atlas_.printTree(std::cout, marks);
}

void Shell::lesionRisk(const std::string& name) const
{
    if (name.empty()) {
        std::cout << "ERROR: Missing area. Usage: lesion risk <area>\n";
        return;
    }
    const Area* area = atlas_.findArea(name);
    if (!area) {
        if (atlas_.findPipe(name))
            std::cout << "ERROR: \"" << name << "\" is a pipe, not an area. Usage: lesion risk <area>\n";
        else
            std::cout << "ERROR: No area called \"" << name << "\".\n";
        return;
    }

    std::vector<const Pipe*> risky = LesionEngine(atlas_).pipesThatStop(*area);
    if (risky.empty()) {
        std::cout << "No single blocked pipe can stop " << name << ".\n";
        return;
    }
    std::cout << counted(risky.size(), "pipe") << (risky.size() == 1 ? " stops " : " stop ")
              << name << " if blocked:\n";
    size_t width = 0;
    for (const Pipe* p : risky) width = std::max(width, p->name.size());
    for (const Pipe* p : risky)
        std::cout << "  " << std::left << std::setw(width + 2) << p->name << p->size << "\n";
}

void Shell::triageCommand(std::istream& args)
{
    std::string sub;
    args >> sub;
    std::string rest = restOf(args);

    if (sub == "load") {
        if (rest.empty()) std::cout << "ERROR: Missing path. Usage: triage load <path>\n";
        else loadSymptoms(rest);
    } else if (sub == "symptoms") {
        listSymptoms();
    } else if (sub == "new") {
        triageNew();
    } else if (sub == "list") {
        triageList();
    } else if (sub == "show") {
        if (rest.empty()) std::cout << "ERROR: Missing id. Usage: triage show <id>\n";
        else triageShow(upper(rest));
    } else if (sub == "update") {
        if (rest.empty()) std::cout << "ERROR: Missing id. Usage: triage update <id>\n";
        else triageUpdate(upper(rest));
    } else if (sub == "urgent") {
        triageUrgent();
    } else {
        std::cout << "ERROR: Unknown triage command. Type \"help\".\n";
    }
}

void Shell::loadSymptoms(const std::string& path)
{
    SymptomList loaded;
    std::vector<AtlasError> errors;
    if (SymptomList::load(path, loaded, errors)) {
        symptoms_ = loaded;
        symptomsPath_ = path;
        std::cout << "SUCCESS: Symptoms loaded from " << path << ": "
                  << counted(symptoms_.all().size(), "symptom") << ".\n";
        return;
    }
    for (const auto& e : errors) {
        std::cout << "ERROR: ";
        if (e.line > 0) std::cout << path << ", line " << e.line << ": ";
        std::cout << e.message << "\n";
    }
    std::cout << "Symptoms not loaded. ";
    if (symptomsPath_.empty()) std::cout << "No symptom list in use.\n";
    else std::cout << "Still using " << symptomsPath_ << ".\n";
}

void Shell::loadCases()
{
    if (cases_.load(CASES_FILE))
        std::cout << "SUCCESS: Cases loaded from " << CASES_FILE << ": "
                  << counted(cases_.all().size(), "case") << ".\n";
    else
        std::cout << "ERROR: " << CASES_FILE << " is damaged. Fix or remove it. "
                  << "New cases cannot be saved until then.\n";
}

bool Shell::triageReady() const
{
    if (atlasPath_.empty()) {
        std::cout << "ERROR: No atlas is loaded. Use: atlas load <path>\n";
        return false;
    }
    if (symptomsPath_.empty()) {
        std::cout << "ERROR: No symptom list is loaded. Use: triage load <path>\n";
        return false;
    }
    return true;
}

void Shell::listSymptoms() const
{
    if (symptomsPath_.empty()) {
        std::cout << "ERROR: No symptom list is loaded. Use: triage load <path>\n";
        return;
    }
    size_t width = 4;
    for (const auto& s : symptoms_.all()) width = std::max(width, s.code.size());
    std::cout << std::left << std::setw(width + 2) << "CODE"
              << std::setw(8) << "POINTS" << "WHAT THE CREW SEES\n";
    for (const auto& s : symptoms_.all())
        std::cout << std::setw(width + 2) << s.code << std::setw(8) << s.points
                  << s.description << "\n";
    std::cout << counted(symptoms_.all().size(), "symptom") << "\n";
}

// Every ask function returns false only when the input ends.
bool Shell::ask(const std::string& question, std::string& answer) const
{
    std::cout << question << std::flush;
    if (!std::getline(std::cin, answer)) return false;
    answer = trim(answer);
    return true;
}

bool Shell::askNotEmpty(const std::string& question, const std::string& what,
                        std::string& answer) const
{
    while (ask(question, answer)) {
        if (!answer.empty()) return true;
        std::cout << "ERROR: " << what << " cannot be empty.\n";
    }
    return false;
}

bool Shell::askAge(int& age) const
{
    std::string answer;
    while (ask("Age: ", answer)) {
        bool valid = !answer.empty() && answer.size() <= 3 &&
            std::all_of(answer.begin(), answer.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; });
        if (valid) {
            age = std::stoi(answer);
            if (age <= 120) return true;
        }
        std::cout << "ERROR: Age must be a whole number from 0 to 120.\n";
    }
    return false;
}

bool Shell::askClock(const std::string& question, int& minutes) const
{
    std::string answer;
    while (ask(question, answer)) {
        if (parseClock(answer, minutes)) return true;
        std::cout << "ERROR: Invalid time \"" << answer << "\". Use HH:MM, for example 08:10.\n";
    }
    return false;
}

bool Shell::askLastSeen(int now, int& lastSeen) const
{
    int clock = 0;
    if (!askClock("Last seen well (HH:MM): ", clock)) return false;

    std::string answer;
    while (ask("Was that yesterday? (yes/no): ", answer)) {
        answer = toLower(answer);
        int corrected = 0;
        if (parseClock(answer, corrected)) {     // lets a mistyped time be fixed here
            clock = corrected;
        } else if (answer != "yes" && answer != "no") {
            std::cout << "ERROR: Answer yes or no.\n";
        } else if (answer == "no" && clock > now) {
            std::cout << "ERROR: Last seen well (" << clockText(clock) << ") is after the time now ("
                      << clockText(now) << "). Was it yesterday?\n";
        } else {
            lastSeen = lastSeenMinutes(clock, answer == "yes");
            return true;
        }
    }
    return false;
}

bool Shell::askSymptoms(const std::string& question, std::vector<std::string>& codes) const
{
    std::string answer;
    while (ask(question, answer)) {
        std::vector<std::string> typed;
        std::string unknown;
        for (const auto& item : split(toLower(answer), ',')) {
            if (item.empty()) continue;
            if (!symptoms_.find(item)) { unknown = item; break; }
            if (std::find(typed.begin(), typed.end(), item) == typed.end())
                typed.push_back(item);
        }
        if (!unknown.empty()) {
            std::cout << "ERROR: Unknown symptom \"" << unknown
                      << "\". Type triage symptoms to see the list.\n";
        } else if (typed.empty()) {
            std::cout << "ERROR: Enter at least one symptom.\n";
        } else {
            codes = typed;
            return true;
        }
    }
    return false;
}

void Shell::triageNew()
{
    if (!triageReady()) return;

    Case c;
    if (!askNotEmpty("Patient name: ", "Name", c.name) || !askAge(c.age) ||
        !askNotEmpty("City: ", "City", c.city) || !askClock("Time now (HH:MM): ", c.timeNow) ||
        !askLastSeen(c.timeNow, c.lastSeen) ||
        !askSymptoms("Symptoms (comma separated): ", c.symptoms)) {
        std::cout << "\nCase not created.\n";
        return;
    }

    c.createdAt = c.timeNow;
    c.result = TriageEngine(atlas_, symptoms_).assess(c.symptoms, c.lastSeen, c.timeNow);
    if (!cases_.add(c)) {
        std::cout << "ERROR: Could not save the case to " << cases_.path() << ".\n";
        return;
    }

    const Assessment& a = c.result;
    std::cout << "SUCCESS: Case " << c.id << " created and saved.\n";
    printCaseField("Severity", std::to_string(a.severity) + " (" + a.band + ")");
    printCaseField("Likely blocked pipe", likelyPipeLine(a));
    printCaseField("Drug possible", drugLine(a, c.timeNow));
    printCaseField("Clot removal", clotLine(a, c.timeNow));
}

void Shell::triageList() const
{
    if (cases_.all().empty()) {
        std::cout << "No cases yet.\n";
        return;
    }
    size_t nameWidth = 4, cityWidth = 4;
    for (const auto& c : cases_.all()) {
        nameWidth = std::max(nameWidth, c.name.size());
        cityWidth = std::max(cityWidth, c.city.size());
    }
    std::cout << std::left << std::setw(8) << "ID" << std::setw(nameWidth + 2) << "NAME"
              << std::setw(5) << "AGE" << std::setw(cityWidth + 2) << "CITY"
              << std::setw(14) << "SEVERITY" << "LIKELY PIPE\n";
    for (const auto& c : cases_.all())
        std::cout << std::setw(8) << c.id << std::setw(nameWidth + 2) << c.name
                  << std::setw(5) << c.age << std::setw(cityWidth + 2) << c.city
                  << std::setw(14) << (std::to_string(c.result.severity) + " " + c.result.band)
                  << c.result.likelyPipe << "\n";
    std::cout << counted(cases_.all().size(), "case") << "\n";
}

void Shell::triageShow(const std::string& id) const
{
    const Case* c = cases_.find(id);
    if (!c) {
        std::cout << "ERROR: No case called \"" << id << "\".\n";
        return;
    }
    const Assessment& a = c->result;
    std::cout << "=== Case " << c->id << ": " << c->name << " ===\n";
    printCaseField("Age", std::to_string(c->age));
    printCaseField("City", c->city);
    printCaseField("Created at", clockText(c->createdAt));
    printCaseField("Last seen well", clockText(c->lastSeen));
    printCaseField("Symptoms", join(c->symptoms, "none"));
    printCaseField("Severity", std::to_string(a.severity) + " (" + a.band + ")");
    printCaseField("Likely blocked pipe", likelyPipeLine(a));
    printCaseField("Drug possible", drugLine(a, c->timeNow));
    printCaseField("Clot removal", clotLine(a, c->timeNow));
    if (c->routed)
        printCaseField("Route", c->route.hospital + ", arrives " + clockText(c->route.arrives));
    if (c->stayed)
        printCaseField("Hospital", "scan " + clockText(c->stay.scanAt) +
                                   ", drug " + timeOrDash(c->stay.drugAt) +
                                   ", clot removal " + timeOrDash(c->stay.clotAt));
    if (c->history.empty()) return;
    std::cout << "History:\n";
    for (const auto& u : c->history)
        std::cout << "  " << clockText(u.time) << "  added " << join(u.addedSymptoms, "none")
                  << ", severity " << u.severity << ", likely pipe " << u.likelyPipe << "\n";
}

void Shell::triageUpdate(const std::string& id)
{
    if (!triageReady()) return;
    const Case* found = cases_.find(id);
    if (!found) {
        std::cout << "ERROR: No case called \"" << id << "\".\n";
        return;
    }

    Case c = *found;
    Assessment before = c.result;
    int now = 0;
    std::vector<std::string> added;
    while (true) {
        if (!askClock("Time now (HH:MM): ", now)) {
            std::cout << "\nCase not updated.\n";
            return;
        }
        if (now >= c.timeNow) break;
        std::cout << "ERROR: Time now (" << clockText(now) << ") is before the last time for this case ("
                  << clockText(c.timeNow) << ").\n";
    }
    if (!askSymptoms("New symptoms (comma separated): ", added)) {
        std::cout << "\nCase not updated.\n";
        return;
    }

    for (const auto& code : added)
        if (std::find(c.symptoms.begin(), c.symptoms.end(), code) == c.symptoms.end())
            c.symptoms.push_back(code);
    c.timeNow = now;
    c.result = TriageEngine(atlas_, symptoms_).assess(c.symptoms, c.lastSeen, now);
    c.routed = false;     // the old route was chosen for the old symptoms
    c.stayed = false;

    CaseUpdate update;
    update.time = now;
    update.addedSymptoms = added;
    update.severity = c.result.severity;
    update.likelyPipe = c.result.likelyPipe;
    c.history.push_back(update);

    if (!cases_.update(c)) {
        std::cout << "ERROR: Could not save the case to " << cases_.path() << ".\n";
        return;
    }

    const Assessment& a = c.result;
    std::cout << "SUCCESS: Case " << c.id << " updated.\n";
    printCaseField("Severity", std::to_string(a.severity) + " (" + a.band + "), was " +
                               std::to_string(before.severity));
    printCaseField("Likely blocked pipe", likelyPipeLine(a) + ", was " + before.likelyPipe);
    printCaseField("Drug possible", drugLine(a, now));
    printCaseField("Clot removal", clotLine(a, now));
}

void Shell::triageUrgent()
{
    int now = 0;
    if (!askClock("Time now (HH:MM): ", now)) return;

    std::vector<const Case*> drug, clot;
    for (const auto& c : cases_.all()) {
        const Assessment& a = c.result;
        if (now <= a.drugUntil) drug.push_back(&c);
        else if (clotAllowed(now, a.clotUntil, a.bigPipe, a.severity)) clot.push_back(&c);
    }
    std::stable_sort(drug.begin(), drug.end(), [](const Case* x, const Case* y) {
        return x->result.drugUntil < y->result.drugUntil;
    });
    std::stable_sort(clot.begin(), clot.end(), [](const Case* x, const Case* y) {
        return x->result.clotUntil < y->result.clotUntil;
    });

    if (drug.empty() && clot.empty()) {
        std::cout << "No cases can still be treated.\n";
        return;
    }
    size_t width = 0;
    for (const auto& c : cases_.all()) width = std::max(width, c.name.size());
    auto printRow = [&](const Case* c, int limit) {
        std::cout << "  " << std::left << std::setw(8) << c->id << std::setw(width + 2) << c->name
                  << "until " << std::setw(16) << clockText(limit)
                  << durationText(limit - now) << " left\n";
    };

    if (!drug.empty()) std::cout << "Drug still possible:\n";
    for (const Case* c : drug) printRow(c, c->result.drugUntil);
    if (!clot.empty()) std::cout << "Only clot removal still possible:\n";
    for (const Case* c : clot) printRow(c, c->result.clotUntil);
}

void Shell::mapCommand(std::istream& args)
{
    std::string sub;
    args >> sub;
    std::string rest = restOf(args);

    if (sub == "load") {
        if (rest.empty()) std::cout << "ERROR: Missing path. Usage: map load <path>\n";
        else loadMap(rest);
    } else {
        std::cout << "ERROR: Unknown map command. Type \"help\".\n";
    }
}

void Shell::loadMap(const std::string& path)
{
    std::string loaded;
    std::vector<AtlasError> errors;
    if (map_.load(path, loaded, errors)) {
        std::cout << "SUCCESS: Map loaded from " << path << ": " << loaded << ".\n";
        return;
    }
    size_t slash = path.find_last_of("/\\");
    std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
    for (const auto& e : errors) {
        std::cout << "ERROR: ";
        if (e.line > 0) std::cout << file << ", line " << e.line << ": ";
        std::cout << e.message << "\n";
    }
    if (map_.hasRoads()) std::cout << "Map not loaded. The previous map stays in use.\n";
    else std::cout << "Map not loaded. No map in use.\n";
}

bool Shell::mapReady() const
{
    if (map_.hasRoads() && map_.hasHospitals()) return true;
    std::cout << "ERROR: No map is loaded. Use: map load <path>\n";
    return false;
}

void Shell::routeCommand(std::istream& args)
{
    std::string sub;
    args >> sub;
    std::string rest = restOf(args);

    if (sub.empty()) {
        std::cout << "ERROR: Missing case id. Usage: route <case id>\n";
    } else if (!mapReady()) {
        return;
    } else if (sub == "from") {
        if (rest.empty()) std::cout << "ERROR: Missing city. Usage: route from <city>\n";
        else routeFrom(rest);
    } else if (sub == "whatif") {
        routeWhatIf(rest);
    } else {
        routeCase(upper(sub));
    }
}

void Shell::routeFrom(const std::string& name) const
{
    int city = map_.cityIndex(name);
    if (city < 0) {
        std::cout << "ERROR: No city called \"" << name << "\" on the map.\n";
        return;
    }
    std::vector<HospitalDrive> drives = Router(map_).hospitalsFrom(city);
    if (drives.empty()) {
        std::cout << "ERROR: No road leads from " << map_.cities()[city] << " to any hospital.\n";
        return;
    }

    size_t nameWidth = 8, cityWidth = 4;
    for (const auto& h : map_.hospitals()) {
        nameWidth = std::max(nameWidth, h.name.size());
        cityWidth = std::max(cityWidth, h.city.size());
    }
    std::cout << std::left << std::setw(nameWidth + 2) << "HOSPITAL" << std::setw(cityWidth + 2)
              << "CITY" << std::setw(16) << "CAN DO" << std::setw(9) << "DRIVE" << "ROUTE\n";
    for (const auto& d : drives)
        std::cout << std::setw(nameWidth + 2) << d.hospital->name
                  << std::setw(cityWidth + 2) << d.hospital->city
                  << std::setw(16) << join(d.hospital->abilities, "none")
                  << std::setw(9) << (std::to_string(d.minutes) + " min")
                  << pathText(d.path) << "\n";
    if (drives.size() < map_.hospitals().size())
        std::cout << (map_.hospitals().size() - drives.size())
                  << " more hospitals cannot be reached by road.\n";
}

void Shell::printDecision(const Case& c, const RouteDecision& d) const
{
    const Assessment& a = c.result;
    std::cout << "Case " << c.id << ": " << c.name << ", " << c.city
              << ", leaves at " << clockText(c.createdAt) << "\n";

    if (a.clotPossible && a.drugPossible)
        std::cout << "Needs: clot removal (until " << clockText(a.clotUntil) << "), drug (until "
                  << clockText(a.drugUntil) << ")\n";
    else if (a.clotPossible)
        std::cout << "Needs: clot removal (until " << clockText(a.clotUntil)
                  << "). The drug limit has passed.\n";
    else if (a.drugPossible)
        std::cout << "Needs: drug (until " << clockText(a.drugUntil)
                  << "). Clot removal is not possible.\n";
    else
        std::cout << "Needs: a scan and care. No treatment is still possible.\n";

    if (d.comparedPlans) {
        const CaseRoute& b = d.planB;
        std::cout << "Plan A, straight to clot removal: " << d.planA.hospital << "\n"
                  << "  " << planSummary(d.planA) << "\n"
                  << "Plan B, drug first, then transfer:\n"
                  << "  " << b.firstHospital << " (" << pathText(b.firstPath) << "): arrives "
                  << clockText(b.firstArrives) << ", drug " << clockText(b.drugAt) << "\n"
                  << "  Transfer leaves " << clockText(b.leaves) << ": " << pathText(b.path)
                  << ", " << b.driveMinutes << " min\n"
                  << "  " << b.hospital << ": arrives " << clockText(b.arrives)
                  << ", clot removal " << clockText(b.clotAt) << "\n"
                  << d.reason << "\n";
    }

    const CaseRoute& r = d.route;
    if (r.transfer) return;      // Plan B is printed in full above

    if (r.treatment == TREATMENT_CLOT)
        std::cout << "Fastest hospital that can remove the clot: " << r.hospital << "\n";
    else if (r.treatment == TREATMENT_DRUG)
        std::cout << "Fastest hospital that can give the drug in time: " << r.hospital << "\n";
    else
        std::cout << "No treatment can be given in time. Fastest hospital with CT: "
                  << r.hospital << "\n";

    printField("Route", pathText(r.path) + ", " + std::to_string(r.driveMinutes) + " min");
    printField("Arrives", clockText(r.arrives));
    if (r.drugAt != NO_TIME)
        printField("Drug", clockText(r.drugAt));
    else if (a.drugPossible && r.treatment == TREATMENT_CLOT)
        printField("Drug", "too late (limit " + clockText(a.drugUntil) + ")");
    if (r.clotAt != NO_TIME) printField("Clot removal", clockText(r.clotAt));
}

void Shell::routeCase(const std::string& id)
{
    const Case* found = cases_.find(id);
    if (!found) {
        std::cout << "ERROR: No case called \"" << id << "\".\n";
        return;
    }
    int city = map_.cityIndex(found->city);
    if (city < 0) {
        std::cout << "ERROR: The city of case " << id << ", \"" << found->city
                  << "\", is not on the map.\n";
        return;
    }

    RouteDecision decision;
    if (!Router(map_).decide(*found, city, decision)) {
        std::cout << "ERROR: " << decision.failure << "\n";
        return;
    }
    printDecision(*found, decision);

    Case updated = *found;
    updated.routed = true;
    updated.route = decision.route;
    updated.stayed = false;
    if (cases_.update(updated)) std::cout << "SUCCESS: Route saved for " << id << ".\n";
    else std::cout << "ERROR: Could not save the route to " << cases_.path() << ".\n";
}

void Shell::routeWhatIf(const std::string& text) const
{
    size_t space = text.find_last_of(" \t");
    if (space == std::string::npos) {
        std::cout << "ERROR: Missing ability. Usage: route whatif <city> <ability>\n";
        return;
    }
    std::string cityName = trim(text.substr(0, space));
    std::string ability = upper(text.substr(space + 1));

    int city = map_.cityIndex(cityName);
    if (city < 0) {
        std::cout << "ERROR: No city called \"" << cityName << "\" on the map.\n";
        return;
    }
    if (ability != ABILITY_CT && ability != ABILITY_DRUG && ability != ABILITY_CLOT) {
        std::cout << "ERROR: Unknown ability \"" << ability << "\". Expected CT, DRUG or CLOT.\n";
        return;
    }
    std::vector<std::string> names;
    for (const auto& h : map_.hospitals())
        if (h.city == map_.cities()[city]) names.push_back(h.name);
    if (names.empty()) {
        std::cout << "ERROR: No hospital in " << map_.cities()[city] << ".\n";
        return;
    }

    CityMap changed = map_;        // only this copy changes; no file is written
    changed.addAbility(city, ability);
    Router before(map_), after(changed);

    auto clotDrive = [](const Router& router, int from) {
        std::vector<HospitalDrive> drives = router.hospitalsFrom(from);
        const HospitalDrive* d = router.nearest(drives, ABILITY_CLOT);
        return d ? d->minutes : NO_ROAD;
    };
    auto minutesText = [](int minutes) {
        return minutes == NO_ROAD ? std::string("no route") : std::to_string(minutes) + " min";
    };

    std::string can = ability == ABILITY_CLOT ? "remove clots"
                    : ability == ABILITY_DRUG ? "give the drug" : "do a CT scan";
    std::cout << "What if " << join(names, "none") << " (" << map_.cities()[city]
              << ") could also " << can << "?\n";
    std::cout << std::left << std::setw(9) << "CASE" << std::setw(14) << "CITY"
              << std::setw(10) << "NOW" << std::setw(17) << "WITH THE CHANGE" << "SAVED\n";

    int total = 0, counted_cases = 0;
    for (const auto& c : cases_.all()) {
        int from = map_.cityIndex(c.city);
        if (!c.result.clotPossible || from < 0) continue;
        int now = clotDrive(before, from), then = clotDrive(after, from);
        int saved = now == NO_ROAD || then == NO_ROAD ? 0 : now - then;
        total += saved;
        ++counted_cases;
        std::cout << std::setw(9) << c.id << std::setw(14) << c.city << std::setw(10)
                  << minutesText(now) << std::setw(17) << minutesText(then) << saved << " min\n";
    }
    std::cout << "Total saved: " << total << " min over " << counted(counted_cases, "case") << ".\n";
}

void Shell::unitCommand(std::istream& args)
{
    std::string sub;
    args >> sub;
    if (sub != "run" && sub != "compare") {
        std::cout << "ERROR: Unknown unit command. Type \"help\".\n";
        return;
    }

    std::string file;
    ScanOrder order = ScanOrder::FirstCome;
    int scanners = 1;
    if (!readMorningOptions(args, file, order, scanners)) return;
    if (file.empty()) {
        std::cout << "ERROR: Missing file. Usage: unit " << sub << " <file>\n";
        return;
    }
    if (sub == "run") unitRun(file, order, scanners);
    else unitCompare(file, order);
}

bool Shell::readMorningOptions(std::istream& args, std::string& file, ScanOrder& order,
                               int& scanners) const
{
    std::string word;
    while (args >> word) {
        if (word == "--order") {
            std::string value;
            args >> value;
            if (value != "deadline" && value != "first") {
                std::cout << "ERROR: Unknown order \"" << value << "\". Expected first or deadline.\n";
                return false;
            }
            order = value == "deadline" ? ScanOrder::Deadline : ScanOrder::FirstCome;
        } else if (word == "--scanners") {
            if (!(args >> scanners) || scanners < 1 || scanners > 20) {
                std::cout << "ERROR: Scanners must be a whole number from 1 to 20.\n";
                return false;
            }
        } else {
            file += (file.empty() ? "" : " ") + word;
        }
    }
    return true;
}

bool Shell::loadMorningFile(const std::string& file, Morning& morning) const
{
    std::vector<AtlasError> errors;
    if (loadMorning(file, cases_, morning, errors)) return true;

    size_t slash = file.find_last_of("/\\");
    std::string name = slash == std::string::npos ? file : file.substr(slash + 1);
    for (const auto& e : errors) {
        std::cout << "ERROR: ";
        if (e.line > 0) std::cout << name << ", line " << e.line << ": ";
        std::cout << e.message << "\n";
    }
    return false;
}

void Shell::printReport(const UnitReport& report) const
{
    for (const auto& e : report.events)
        std::cout << clockText(e.time) << "  " << e.id << " " << e.text << "\n";

    std::cout << "\n" << std::left << std::setw(9) << "PATIENT" << std::setw(9) << "ARRIVES"
              << std::setw(8) << "SCAN" << std::setw(8) << "DRUG" << std::setw(15) << "DOOR-TO-DRUG"
              << std::setw(14) << "CLOT REMOVAL" << "NOTE\n";

    for (const auto& p : report.patients) {
        std::string door = "-", note;
        if (p.drugAt != NO_TIME) {
            door = std::to_string(p.doorToDrug()) + " min";
            if (p.doorToDrug() > SLOW_DOOR_TO_DRUG) door += " SLOW";
        }
        if (p.isHead) note = p.scanLate() ? "HEAD SCAN LATE (" + std::to_string(p.scanAt - p.arrives) +
                                            " min)" : "head injury";
        else if (!p.drugGivenAt.empty()) note = "drug given at " + p.drugGivenAt;
        else if (p.missedDrug) note = "MISSED THE DRUG (limit " + clockText(p.drugLimit) + ")";
        else if (!p.canGetDrug()) note = "drug limit passed before arrival";

        std::cout << std::setw(9) << p.id << std::setw(9) << clockText(p.arrives)
                  << std::setw(8) << clockText(p.scanAt) << std::setw(8) << timeOrDash(p.drugAt)
                  << std::setw(15) << door << std::setw(14) << timeOrDash(p.clotAt)
                  << note << "\n";
    }

    std::cout << "\nDrug given to " << report.drugGiven << " of " << report.couldGetDrug
              << " patients who could get it.";
    if (report.drugGiven > 0)
        std::cout << " Average door-to-drug: " << report.averageDoorToDrug << " min.";
    std::cout << "\n";
    if (!report.lateHeads.empty())
        std::cout << "Head scans late: " << join(report.lateHeads, "none") << "\n";
}

void Shell::saveStays(const UnitReport& report)
{
    std::vector<std::string> saved;
    for (const auto& p : report.patients) {
        if (!p.fromCase) continue;
        Case c = *cases_.find(p.id);
        c.stayed = true;
        c.stay.hospital = UNIT_HOSPITAL;
        c.stay.arrives = p.arrives;
        c.stay.scanAt = p.scanAt;
        c.stay.drugAt = p.drugAt;
        c.stay.clotAt = p.clotAt;
        if (!cases_.update(c)) {
            std::cout << "ERROR: Could not save the hospital times to " << cases_.path() << ".\n";
            return;
        }
        saved.push_back(p.id);
    }
    std::sort(saved.begin(), saved.end());
    if (!saved.empty())
        std::cout << "SUCCESS: Hospital times saved for " << join(saved, "none") << ".\n";
}

void Shell::unitRun(const std::string& file, ScanOrder order, int scanners)
{
    Morning morning;
    if (!loadMorningFile(file, morning)) return;

    for (const auto& id : morning.noRoute)
        std::cout << "Case " << id << " has no route yet. Run route " << id << " first. Skipped.\n";
    for (const auto& id : morning.duplicates)
        std::cout << "Case " << id << " is also in " << file << ". Using the file. Skipped.\n";
    if (morning.patients.empty()) {
        std::cout << "No patients arrive this morning.\n";
        return;
    }

    std::cout << UNIT_HOSPITAL << " - " << scanners << " CT scanner" << (scanners == 1 ? "" : "s")
              << " - " << (order == ScanOrder::Deadline ? "deadline first" : "first come, first served")
              << "\n"
              << counted(morning.patients.size(), "patient") << ": " << morning.fromFile
              << " from " << file << ", " << morning.fromCases << " from your cases";
    if (!morning.elsewhere.empty())
        std::cout << " (" << join(morning.elsewhere, "none")
                  << (morning.elsewhere.size() == 1 ? " goes" : " go") << " to another hospital)";
    std::cout << "\n\n";

    UnitReport report = StrokeUnit(scanners, order).run(morning.patients);
    printReport(report);
    saveStays(report);
}

void Shell::unitCompare(const std::string& file, ScanOrder order) const
{
    Morning morning;
    if (!loadMorningFile(file, morning)) return;
    if (morning.patients.empty()) {
        std::cout << "No patients arrive this morning.\n";
        return;
    }

    UnitReport one = StrokeUnit(1, order).run(morning.patients);
    UnitReport two = StrokeUnit(2, order).run(morning.patients);

    size_t width = 12;
    for (const auto* list : {&one.slow, &one.missed, &one.lateHeads})
        width = std::max(width, join(*list, "none").size() + 2);
    auto row = [&](const std::string& label, const std::string& a, const std::string& b) {
        std::cout << std::left << std::setw(24) << label << std::setw(width) << a << b << "\n";
    };
    auto given = [](const UnitReport& r) {
        return std::to_string(r.drugGiven) + " of " + std::to_string(r.couldGetDrug);
    };
    auto average = [](const UnitReport& r) {
        return r.drugGiven == 0 ? std::string("-") : std::to_string(r.averageDoorToDrug) + " min";
    };

    row("", "1 SCANNER", "2 SCANNERS");
    row("Drug given", given(one), given(two));
    row("Average door-to-drug", average(one), average(two));
    row("SLOW (over 60 min)", join(one.slow, "none"), join(two.slow, "none"));
    row("Missed the drug", join(one.missed, "none"), join(two.missed, "none"));
    row("Head scans late", join(one.lateHeads, "none"), join(two.lateHeads, "none"));
}

void Shell::caseCommand(std::istream& args)
{
    std::string sub;
    args >> sub;
    std::string rest = restOf(args);
    std::string word = toLower(sub);

    if (sub.empty()) std::cout << "ERROR: Missing case id. Usage: case <case id>\n";
    else if (word == "find") caseFind(rest);
    else if (word == "cost") {
        if (rest.empty()) std::cout << "ERROR: Missing case id. Usage: case cost <case id>\n";
        else caseCost(upper(rest));
    } else caseShow(upper(sub));
}

// The whole story of one case, built only from what the other parts saved.
std::string Shell::caseText(const Case& c) const
{
    const Assessment& a = c.result;
    std::ostringstream os;
    os << "=== Case " << c.id << ": " << c.name << ", " << c.age << ", " << c.city << " ===\n";

    os << "TRIAGE\n"
       << timeRow(c.lastSeen, "Last seen well")
       << timeRow(c.createdAt, "Case created by the ambulance crew")
       << "       Symptoms: " << join(c.symptoms, "none") << "\n"
       << "       Severity " << a.severity << " (" << a.band << "), likely blocked pipe: "
       << pipeText(a) << "\n"
       << "       Drug: " << drugLine(a, c.timeNow) << "\n"
       << "       Clot removal: " << clotLine(a, c.timeNow) << "\n";

    if (!c.routed) {
        os << "ROUTE: not planned yet. Run route " << c.id << ".\n";
    } else {
        const CaseRoute& r = c.route;
        os << "ROUTE\n";
        if (r.transfer) {
            os << timeRow(r.firstLeaves, "Leaves " + c.city + ": " + pathText(r.firstPath) + ", " +
                                             std::to_string(r.firstDriveMinutes) + " min")
               << timeRow(r.firstArrives, "Arrives at " + r.firstHospital)
               << timeRow(r.leaves, "Transfer leaves " + r.firstHospital + ": " + pathText(r.path) +
                                        ", " + std::to_string(r.driveMinutes) + " min");
        } else {
            os << timeRow(r.leaves, "Leaves " + c.city + ": " + pathText(r.path) + ", " +
                                        std::to_string(r.driveMinutes) + " min");
        }
        os << timeRow(r.arrives, "Arrives at " + r.hospital);
    }

    if (!c.routed) {
        os << "HOSPITAL: not planned yet, because there is no route.\n";
    } else if (!c.stayed && c.route.hospital != UNIT_HOSPITAL) {
        os << "HOSPITAL\nNot recorded: only " << UNIT_HOSPITAL << " is simulated.";
        if (c.route.drugAt != NO_TIME) os << " Planned drug: " << clockText(c.route.drugAt) << ".";
        os << "\n";
    } else {
        os << std::left << std::setw(17) << "HOSPITAL" << std::setw(10) << "PLANNED"
           << std::setw(10) << "ACTUAL" << "WAITED\n";
        os << stageRow("Scan", NO_TIME, c.stayed ? c.stay.scanAt : NO_TIME);
        os << stageRow("Drug", c.route.drugAt, c.stayed ? c.stay.drugAt : NO_TIME);
        os << stageRow("Clot removal", c.route.clotAt, c.stayed ? c.stay.clotAt : NO_TIME);
        if (!c.stayed) os << "Not recorded yet. Run unit run <file> first.\n";
    }

    // This version grows from v.5, so it has no clinic visits to show.
    os << "RECOVERY\nNo clinic visits yet.\n";
    return os.str();
}

void Shell::caseShow(const std::string& id) const
{
    const Case* c = cases_.find(id);
    if (!c) {
        std::cout << "ERROR: No case called \"" << id << "\".\n";
        return;
    }
    std::cout << caseText(*c);
}

void Shell::caseFind(const std::string& text) const
{
    if (text.empty()) {
        std::cout << "ERROR: Search text cannot be empty.\n";
        return;
    }
    std::vector<const Case*> found = findCases(cases_.all(), text);
    if (found.empty()) {
        std::cout << "No cases match \"" << text << "\".\n";
        return;
    }
    if (found.size() == 1) {
        std::cout << "1 case matches \"" << text << "\". Opening it.\n" << caseText(*found[0]);
        return;
    }

    size_t nameWidth = 4;
    for (const Case* c : found) nameWidth = std::max(nameWidth, c->name.size());
    std::cout << found.size() << " cases match \"" << text << "\":\n";
    for (size_t i = 0; i < found.size(); ++i) {
        const Case& c = *found[i];
        std::cout << (i + 1) << ". " << std::left << std::setw(8) << c.id
                  << std::setw(nameWidth + 2) << c.name << std::setw(5) << c.age << c.city << "\n";
    }

    std::string answer;
    if (!ask("Enter a number to open, or press ENTER to go back: ", answer)) return;
    if (answer.empty()) return;

    bool digits = answer.size() <= 4 && answer.find_first_not_of("0123456789") == std::string::npos;
    int pick = digits ? std::atoi(answer.c_str()) : 0;
    if (pick < 1 || pick > (int)found.size()) {
        std::cout << "ERROR: Enter a number from 1 to " << found.size() << ".\n";
        return;
    }
    std::cout << caseText(*found[pick - 1]);
}

void Shell::caseCost(const std::string& id) const
{
    const Case* c = cases_.find(id);
    if (!c) {
        std::cout << "ERROR: No case called \"" << id << "\".\n";
        return;
    }
    FirstTreatment first = firstTreatment(*c);
    if (!first.found) {
        std::cout << "No treatment time is recorded yet, so the cost cannot be counted.\n";
        return;
    }
    int minutes = first.minutes - c->lastSeen;
    long cells = (long)minutes * BRAIN_CELLS_PER_MINUTE;
    std::cout << "Last seen well " << clockText(c->lastSeen) << " -> first treatment "
              << clockText(first.minutes) << " (" << first.kind << (first.actual ? "" : ", planned")
              << "): " << minutes << " min\n";
    std::cout << "About " << millionsText(cells) << " brain cells lost before treatment, at "
              << millionsText(BRAIN_CELLS_PER_MINUTE) << " a minute.\n";
    if (first.waitKnown && first.waited > 0)
        std::cout << first.waited << " of those minutes were spent waiting in the hospital line: about "
                  << millionsText((long)first.waited * BRAIN_CELLS_PER_MINUTE) << ".\n";
    else if (first.waitKnown)
        std::cout << "No minutes were spent waiting in the hospital line.\n";
    std::cout << "The " << millionsText(BRAIN_CELLS_PER_MINUTE)
              << " figure is a published average for large strokes, used here as an estimate.\n";
}

void Shell::help() const
{
    std::cout << "atlas load <path>   load another atlas file\n"
              << "atlas pipes         list every pipe\n"
              << "atlas areas         list every area\n"
              << "atlas show <name>   show one pipe or area in full\n"
              << "atlas find <word>   find areas by name, symptom or description\n"
              << "atlas tree          draw the network from the heart\n"
              << "atlas path <area>   list every route from the heart to an area\n"
              << "lesion block <pipes>  show what stops working (pipes separated by commas)\n"
              << "lesion risk <area>    list every pipe that alone would stop an area\n"
              << "lesion tree <pipes>   draw the network and mark what a clot affects\n"
              << "triage load <path>    load another symptom list\n"
              << "triage symptoms       list every symptom\n"
              << "triage new            create a patient case\n"
              << "triage list           list every case\n"
              << "triage show <id>      show one case in full\n"
              << "triage update <id>    add new symptoms to a case\n"
              << "triage urgent         list the cases that can still be treated\n"
              << "map load <path>       load a new roads or hospitals file\n"
              << "route from <city>     list every hospital, fastest first\n"
              << "route <case id>       choose and save a hospital for a case\n"
              << "route whatif <city> <ability>   compare drives if a hospital could do more\n"
              << "unit run <file>       replay a morning at the hospital\n"
              << "                      options: --order first|deadline, --scanners <n>\n"
              << "unit compare <file>   compare one and two scanners (--order works here too)\n"
              << "case <id>           show one patient's whole story\n"
              << "case find <text>     search cases by name or city\n"
              << "case cost <id>       minutes and brain cells lost before treatment\n"
              << "help                show this list\n"
              << "exit                leave Synapse\n";
}

int main()
{
    Shell shell;
    shell.run();
    return 0;
}
