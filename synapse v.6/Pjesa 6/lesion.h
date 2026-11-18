#ifndef LESION_H
#define LESION_H

#include "atlas.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct SavedPipe
{
    const Pipe* pipe;
    std::string from;     // the pipe that still gives it blood
};

struct LesionResult
{
    std::vector<const Pipe*> blocked;
    std::vector<const Pipe*> noBlood;    // in atlas order, blocked pipes included
    std::vector<const Area*> stopped;
    std::vector<std::string> symptoms;   // each listed once
    std::vector<SavedPipe> saved;        // have blood only thanks to a bridge
};

// The one place where the blood rules live. Every command that needs to know
// what a clot does, now or in later parts, asks this class.
class LesionEngine
{
public:
    explicit LesionEngine(const Atlas& atlas) : atlas_(atlas) {}

    LesionResult block(const std::vector<std::string>& pipes) const;
    std::vector<const Pipe*> pipesThatStop(const Area& area) const;

private:
    // Each pipe with blood, mapped to the pipe that gave it blood.
    typedef std::unordered_map<std::string, std::string> BloodMap;

    BloodMap traceBlood(const std::unordered_set<std::string>& blocked, bool useBridges) const;
    static bool areaWorks(const Area& area, const BloodMap& blood);

    const Atlas& atlas_;
};

#endif // LESION_H
