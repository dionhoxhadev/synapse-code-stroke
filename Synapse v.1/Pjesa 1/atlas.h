#ifndef ATLAS_H
#define ATLAS_H

#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

std::string toLower(std::string s);

struct Pipe
{
    std::string name;
    std::string size;                    // "big" or "small"
    std::vector<std::string> comesFrom;  // empty for the heart
    std::vector<std::string> branches;   // worked out from the other pipes
    std::vector<std::string> feeds;      // worked out from the areas
    int line = 0;

    bool isHeart() const { return comesFrom.empty(); }
};

struct Area
{
    std::string name;
    std::vector<std::string> fedBy;
    std::vector<std::string> symptoms;
    std::string description;
    int line = 0;

    bool matches(const std::string& lowerWord) const;
};

struct AtlasError
{
    int line;                 // 0 when the error is about the whole file
    std::string message;
};

class Atlas
{
public:
    // All or nothing: out only changes when the file has no errors.
    static bool load(const std::string& path, Atlas& out, std::vector<AtlasError>& errors);

    const std::vector<Pipe>& pipes() const { return pipes_; }
    const std::vector<Area>& areas() const { return areas_; }
    const Pipe* findPipe(const std::string& name) const;
    const Area* findArea(const std::string& name) const;

    std::vector<const Area*> areasMatching(const std::string& word) const;
    std::vector<std::vector<std::string>> routesTo(const Area& area) const;
    void printTree(std::ostream& os) const;

private:
    void link();
    void checkLoops(std::vector<AtlasError>& errors) const;
    void findLoops(int pipe, std::vector<int>& state, std::vector<int>& stack,
                   std::vector<AtlasError>& errors) const;
    void checkReachable(const std::unordered_set<std::string>& broken,
                        std::vector<AtlasError>& errors) const;
    void walkBack(const Pipe& pipe, std::vector<std::string>& tail,
                  std::vector<std::vector<std::string>>& routes) const;
    void printPipe(std::ostream& os, const Pipe& pipe, int depth,
                   std::unordered_set<std::string>& seen) const;

    std::vector<Pipe> pipes_;            // in file order
    std::vector<Area> areas_;
    std::unordered_map<std::string, int> pipeIndex_;
    std::unordered_map<std::string, int> areaIndex_;
};

#endif // ATLAS_H
