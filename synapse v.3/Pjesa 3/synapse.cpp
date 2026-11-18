// Synapse: command-line engine of the stroke command center.
#include "atlas.h"
#include "cases.h"
#include "lesion.h"
#include "triage.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>

static const std::string DEFAULT_ATLAS = "data/atlas.txt";
static const std::string DEFAULT_SYMPTOMS = "data/symptoms.txt";
static const std::string CASES_FILE = "data/cases.json";

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

// Everything after the current read position, without the outer spaces.
static std::string restOf(std::istream& in)
{
    std::string rest;
    std::getline(in, rest);
    size_t first = rest.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    return rest.substr(first, rest.find_last_not_of(" \t") - first + 1);
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
    void help() const;

    Atlas atlas_;
    std::string atlasPath_;   // empty until an atlas is loaded
    SymptomList symptoms_;
    std::string symptomsPath_;   // empty until a list is loaded
    CaseStore cases_;
};

void Shell::run()
{
    std::cout << "Synapse 1.0 - type \"help\" to see all commands\n";
    loadAtlas(DEFAULT_ATLAS);
    loadSymptoms(DEFAULT_SYMPTOMS);
    loadCases();

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
              << "help                show this list\n"
              << "exit                leave Synapse\n";
}

int main()
{
    Shell shell;
    shell.run();
    return 0;
}
