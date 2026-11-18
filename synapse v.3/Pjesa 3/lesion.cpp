#include "lesion.h"
#include <algorithm>
#include <queue>

// Walks forward from the heart, so a pipe gets blood as soon as one of its
// sources has it. Each pipe enters the queue once, so a circle of bridges
// cannot make this run forever.
LesionEngine::BloodMap LesionEngine::traceBlood(const std::unordered_set<std::string>& blocked,
                                                bool useBridges) const
{
    BloodMap blood;
    const Pipe* heart = atlas_.heart();
    if (!heart) return blood;

    std::queue<const Pipe*> todo;
    blood[heart->name] = "";
    todo.push(heart);

    auto giveBlood = [&](const std::string& name, const std::string& from) {
        if (blocked.count(name) || blood.count(name)) return;
        blood[name] = from;
        todo.push(atlas_.findPipe(name));
    };

    while (!todo.empty()) {
        const Pipe* cur = todo.front();
        todo.pop();
        for (const auto& b : cur->branches) giveBlood(b, cur->name);
        if (useBridges)
            for (const auto& b : cur->bridgedTo) giveBlood(b, cur->name);
    }
    return blood;
}

bool LesionEngine::areaWorks(const Area& area, const BloodMap& blood)
{
    for (const auto& pipe : area.fedBy)
        if (blood.count(pipe)) return true;
    return false;
}

LesionResult LesionEngine::block(const std::vector<std::string>& pipes) const
{
    LesionResult r;
    std::unordered_set<std::string> blocked(pipes.begin(), pipes.end());
    BloodMap blood = traceBlood(blocked, true);
    BloodMap withoutBridges;
    if (atlas_.bridgeCount() > 0) withoutBridges = traceBlood(blocked, false);

    for (const auto& name : pipes)
        if (const Pipe* p = atlas_.findPipe(name)) r.blocked.push_back(p);

    for (const auto& p : atlas_.pipes()) {
        auto it = blood.find(p.name);
        if (it == blood.end())
            r.noBlood.push_back(&p);
        else if (atlas_.bridgeCount() > 0 && !withoutBridges.count(p.name))
            r.saved.push_back({&p, it->second});
    }

    for (const auto& a : atlas_.areas()) {
        if (areaWorks(a, blood)) continue;
        r.stopped.push_back(&a);
        for (const auto& s : a.symptoms)
            if (std::find(r.symptoms.begin(), r.symptoms.end(), s) == r.symptoms.end())
                r.symptoms.push_back(s);
    }
    return r;
}

std::vector<const Pipe*> LesionEngine::pipesThatStop(const Area& area) const
{
    std::vector<const Pipe*> out;
    for (const auto& p : atlas_.pipes())
        if (!p.isHeart() && !areaWorks(area, traceBlood({p.name}, true)))
            out.push_back(&p);
    return out;
}
