#include "atlas.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <queue>

static const int PIPE_FIELDS = 4;
static const int AREA_FIELDS = 5;
static const std::string NO_SOURCE = "-";    // "comes from" of the heart

static std::string trim(const std::string& s)
{
    const char* space = " \t\r\n";
    size_t first = s.find_first_not_of(space);
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(space) - first + 1);
}

static std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    size_t start = 0, pos;
    while ((pos = s.find(sep, start)) != std::string::npos) {
        out.push_back(trim(s.substr(start, pos - start)));
        start = pos + 1;
    }
    out.push_back(trim(s.substr(start)));
    return out;
}

static std::string quoted(const std::string& s)
{
    return "\"" + s + "\"";
}

static bool validName(const std::string& name)
{
    if (name.empty() || name == NO_SOURCE) return false;
    for (unsigned char c : name)
        if (!std::islower(c) && !std::isdigit(c) && c != '-') return false;
    return true;
}

static std::vector<std::string> parseList(const std::string& field, const std::string& label,
                                          int line, std::vector<AtlasError>& errors)
{
    std::vector<std::string> out;
    for (const auto& item : split(field, ',')) {
        if (item.empty())
            errors.push_back({line, quoted(label) + " has an empty name."});
        else if (std::find(out.begin(), out.end(), item) != out.end())
            errors.push_back({line, quoted(label) + " lists " + quoted(item) + " twice."});
        else
            out.push_back(item);
    }
    return out;
}

std::string toLower(std::string s)
{
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool Area::matches(const std::string& lowerWord) const
{
    if (toLower(name).find(lowerWord) != std::string::npos) return true;
    if (toLower(description).find(lowerWord) != std::string::npos) return true;
    for (const auto& s : symptoms)
        if (toLower(s).find(lowerWord) != std::string::npos) return true;
    return false;
}

bool Atlas::load(const std::string& path, Atlas& out, std::vector<AtlasError>& errors)
{
    errors.clear();
    std::ifstream f(path);
    if (!f) {
        errors.push_back({0, "File not found: " + path});
        return false;
    }

    Atlas a;
    std::unordered_map<std::string, int> firstLine;   // every name, pipe or area
    std::unordered_set<std::string> broken;           // pipes we could not fully read
    std::unordered_set<std::string> unreadable;       // names on lines of unknown type
    int lineNo = 0, records = 0, hearts = 0;
    bool incompletePipe = false;
    std::string line;

    while (std::getline(f, line)) {
        ++lineNo;
        if (lineNo == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);   // BOM
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        ++records;

        std::vector<std::string> fields = split(line, '|');
        const std::string& type = fields[0];
        if (type != "PIPE" && type != "AREA") {
            errors.push_back({lineNo, "unknown record type " + quoted(type) +
                                      ". Expected PIPE or AREA."});
            if (fields.size() >= 2) unreadable.insert(fields[1]);
            incompletePipe = true;
            continue;
        }

        bool isPipe = type == "PIPE";
        int needed = isPipe ? PIPE_FIELDS : AREA_FIELDS;
        bool complete = (int)fields.size() == needed;
        if (!complete)
            errors.push_back({lineNo, type + " needs " + std::to_string(needed) +
                                      " fields, found " + std::to_string(fields.size()) + "."});
        if (fields.size() < 2) continue;

        const std::string& name = fields[1];
        if (!validName(name))
            errors.push_back({lineNo, "invalid name " + quoted(name) +
                                      ". Names use lowercase letters, digits and hyphens."});
        auto dup = firstLine.find(name);
        if (dup != firstLine.end()) {
            errors.push_back({lineNo, quoted(name) + " is defined twice (first on line " +
                                      std::to_string(dup->second) + ")."});
            continue;
        }
        firstLine[name] = lineNo;

        if (isPipe) {
            Pipe p;
            p.name = name;
            p.line = lineNo;
            if (complete) {
                p.size = fields[2];
                if (p.size != "big" && p.size != "small")
                    errors.push_back({lineNo, "size must be big or small, found " +
                                              quoted(p.size) + "."});
                if (fields[3] == NO_SOURCE) ++hearts;
                else p.comesFrom = parseList(fields[3], "comes from", lineNo, errors);
            } else {
                incompletePipe = true;
                broken.insert(name);
            }
            a.pipeIndex_[name] = (int)a.pipes_.size();
            a.pipes_.push_back(p);
        } else if (complete) {
            Area ar;
            ar.name = name;
            ar.line = lineNo;
            ar.fedBy = parseList(fields[2], "fed by", lineNo, errors);
            ar.symptoms = parseList(fields[3], "symptoms", lineNo, errors);
            ar.description = fields[4];
            if (ar.description.empty())
                errors.push_back({lineNo, "area " + quoted(name) + " has no description."});
            a.areaIndex_[name] = (int)a.areas_.size();
            a.areas_.push_back(ar);
        }
    }

    if (records == 0) {
        errors.push_back({0, "The file is empty."});
        return false;
    }

    // Names may be defined further down, so references are checked only now.
    for (const auto& p : a.pipes_)
        for (const auto& from : p.comesFrom)
            if (!a.findPipe(from)) {
                if (!unreadable.count(from))
                    errors.push_back({p.line, "pipe " + quoted(p.name) + " comes from " +
                                              quoted(from) + ", which does not exist."});
                broken.insert(p.name);
            }
    for (const auto& ar : a.areas_)
        for (const auto& pipe : ar.fedBy)
            if (!a.findPipe(pipe) && !unreadable.count(pipe))
                errors.push_back({ar.line, "area " + quoted(ar.name) + " is fed by " +
                                           quoted(pipe) + ", which does not exist."});

    std::stable_sort(errors.begin(), errors.end(),
        [](const AtlasError& x, const AtlasError& y) { return x.line < y.line; });

    if (hearts != 1 && !(hearts == 0 && incompletePipe))
        errors.push_back({0, "The atlas needs exactly one pipe that comes from \"-\" "
                             "(the heart). Found " + std::to_string(hearts) + "."});

    a.link();
    a.checkLoops(errors);
    if (hearts == 1) a.checkReachable(broken, errors);

    if (!errors.empty()) return false;
    out = a;
    return true;
}

const Pipe* Atlas::findPipe(const std::string& name) const
{
    auto it = pipeIndex_.find(name);
    return it == pipeIndex_.end() ? nullptr : &pipes_[it->second];
}

const Area* Atlas::findArea(const std::string& name) const
{
    auto it = areaIndex_.find(name);
    return it == areaIndex_.end() ? nullptr : &areas_[it->second];
}

// The file only says where blood comes from; this fills in where it goes.
void Atlas::link()
{
    for (const auto& p : pipes_)
        for (const auto& from : p.comesFrom) {
            auto it = pipeIndex_.find(from);
            if (it != pipeIndex_.end()) pipes_[it->second].branches.push_back(p.name);
        }
    for (const auto& ar : areas_)
        for (const auto& pipe : ar.fedBy) {
            auto it = pipeIndex_.find(pipe);
            if (it != pipeIndex_.end()) pipes_[it->second].feeds.push_back(ar.name);
        }
}

void Atlas::checkLoops(std::vector<AtlasError>& errors) const
{
    std::vector<int> state(pipes_.size(), 0);   // 0 new, 1 on stack, 2 done
    std::vector<int> stack;
    for (int i = 0; i < (int)pipes_.size(); ++i)
        if (state[i] == 0) findLoops(i, state, stack, errors);
}

void Atlas::findLoops(int pipe, std::vector<int>& state, std::vector<int>& stack,
                      std::vector<AtlasError>& errors) const
{
    state[pipe] = 1;
    stack.push_back(pipe);
    for (const auto& from : pipes_[pipe].comesFrom) {
        auto it = pipeIndex_.find(from);
        if (it == pipeIndex_.end()) continue;
        int next = it->second;
        if (state[next] == 0) {
            findLoops(next, state, stack, errors);
        } else if (state[next] == 1) {
            auto start = std::find(stack.begin(), stack.end(), next);
            std::string msg = "Loop: " + pipes_[*start].name + " comes from ";
            for (auto s = start + 1; s != stack.end(); ++s)
                msg += pipes_[*s].name + ", which comes from ";
            errors.push_back({0, msg + from + "."});
        }
    }
    stack.pop_back();
    state[pipe] = 2;
}

void Atlas::checkReachable(const std::unordered_set<std::string>& broken,
                           std::vector<AtlasError>& errors) const
{
    // Broken pipes count as reached, so one typo is not reported again
    // for every pipe after it.
    std::vector<bool> reached(pipes_.size(), false);
    std::queue<int> todo;
    for (int i = 0; i < (int)pipes_.size(); ++i)
        if (pipes_[i].isHeart() || broken.count(pipes_[i].name)) {
            reached[i] = true;
            todo.push(i);
        }
    while (!todo.empty()) {
        int cur = todo.front();
        todo.pop();
        for (const auto& b : pipes_[cur].branches) {
            int next = pipeIndex_.at(b);
            if (!reached[next]) {
                reached[next] = true;
                todo.push(next);
            }
        }
    }
    for (int i = 0; i < (int)pipes_.size(); ++i)
        if (!reached[i])
            errors.push_back({0, "Pipe " + quoted(pipes_[i].name) +
                                 " can never receive blood from the heart."});
}

std::vector<const Area*> Atlas::areasMatching(const std::string& word) const
{
    std::vector<const Area*> out;
    std::string lower = toLower(word);
    for (const auto& ar : areas_)
        if (ar.matches(lower)) out.push_back(&ar);
    return out;
}

std::vector<std::vector<std::string>> Atlas::routesTo(const Area& area) const
{
    std::vector<std::vector<std::string>> routes;
    std::vector<std::string> tail{area.name};
    for (const auto& pipe : area.fedBy)
        walkBack(*findPipe(pipe), tail, routes);
    return routes;
}

// Follows the blood backwards from an area up to the heart.
void Atlas::walkBack(const Pipe& pipe, std::vector<std::string>& tail,
                     std::vector<std::vector<std::string>>& routes) const
{
    tail.push_back(pipe.name);
    if (pipe.isHeart())
        routes.emplace_back(tail.rbegin(), tail.rend());
    for (const auto& from : pipe.comesFrom)
        walkBack(*findPipe(from), tail, routes);
    tail.pop_back();
}

void Atlas::printTree(std::ostream& os) const
{
    std::unordered_set<std::string> seen;
    for (const auto& p : pipes_)
        if (p.isHeart()) printPipe(os, p, 0, seen);
}

void Atlas::printPipe(std::ostream& os, const Pipe& pipe, int depth,
                      std::unordered_set<std::string>& seen) const
{
    os << std::string(depth * 4, ' ') << pipe.name;
    if (!seen.insert(pipe.name).second) {
        os << " (see above)\n";
        return;
    }
    os << "\n";
    for (const auto& b : pipe.branches)
        printPipe(os, *findPipe(b), depth + 1, seen);
    for (const auto& ar : pipe.feeds) {
        os << std::string((depth + 1) * 4, ' ') << '[' << ar << ']';
        if (!seen.insert(ar).second) os << " (see above)";
        os << "\n";
    }
}
