// Synapse: command-line engine of the stroke command center.
#include "atlas.h"
#include "lesion.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

static const std::string DEFAULT_ATLAS = "data/atlas.txt";

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
    void help() const;

    Atlas atlas_;
    std::string atlasPath_;   // empty until an atlas is loaded
};

void Shell::run()
{
    std::cout << "Synapse 1.0 - type \"help\" to see all commands\n";
    loadAtlas(DEFAULT_ATLAS);

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
              << "help                show this list\n"
              << "exit                leave Synapse\n";
}

int main()
{
    Shell shell;
    shell.run();
    return 0;
}
