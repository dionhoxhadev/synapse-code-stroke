#include "cases.h"
#include "json.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

static std::string formatId(int number)
{
    std::ostringstream os;
    os << "P-" << std::setfill('0') << std::setw(4) << number;
    return os.str();
}

static int idNumber(const std::string& id)
{
    if (id.size() < 3 || id.compare(0, 2, "P-") != 0) return 0;
    return std::atoi(id.c_str() + 2);
}

static Json timeOrNull(int minutes)
{
    return minutes == NO_TIME ? Json() : Json::of(clockText(minutes));
}

static Json routeToJson(const CaseRoute& r)
{
    Json j = Json::object();
    j.set("treatment", Json::of(r.treatment));
    j.set("hospital", Json::of(r.hospital));
    j.set("path", Json::of(r.path));
    j.set("driveMinutes", Json::of(r.driveMinutes));
    j.set("leaves", Json::of(clockText(r.leaves)));
    j.set("arrives", Json::of(clockText(r.arrives)));
    j.set("drugAt", timeOrNull(r.drugAt));
    j.set("clotRemovalAt", timeOrNull(r.clotAt));
    j.set("transfer", Json::of(r.transfer));
    if (r.transfer) {
        j.set("firstHospital", Json::of(r.firstHospital));
        j.set("firstPath", Json::of(r.firstPath));
        j.set("firstDriveMinutes", Json::of(r.firstDriveMinutes));
        j.set("firstLeaves", Json::of(clockText(r.firstLeaves)));
        j.set("firstArrives", Json::of(clockText(r.firstArrives)));
    }
    return j;
}

static Json stayToJson(const HospitalStay& s)
{
    Json j = Json::object();
    j.set("hospital", Json::of(s.hospital));
    j.set("arrives", Json::of(clockText(s.arrives)));
    j.set("scanAt", Json::of(clockText(s.scanAt)));
    j.set("drugAt", timeOrNull(s.drugAt));
    j.set("doorToDrugMinutes", s.drugAt == NO_TIME ? Json() : Json::of(s.drugAt - s.arrives));
    j.set("clotRemovalAt", timeOrNull(s.clotAt));
    return j;
}

static Json toJson(const Case& c)
{
    // The last seen clock stays one field and the day another, as people read them.
    bool yesterday = c.lastSeen < 0;
    std::string lastSeenClock = clockText(yesterday ? c.lastSeen + 24 * 60 : c.lastSeen);

    Json j = Json::object();
    j.set("id", Json::of(c.id));
    j.set("name", Json::of(c.name));
    j.set("age", Json::of(c.age));
    j.set("city", Json::of(c.city));
    j.set("createdAt", Json::of(clockText(c.createdAt)));
    j.set("timeNow", Json::of(clockText(c.timeNow)));
    j.set("lastSeenWell", Json::of(lastSeenClock));
    j.set("lastSeenWellYesterday", Json::of(yesterday));
    j.set("symptoms", Json::of(c.symptoms));
    j.set("severity", Json::of(c.result.severity));
    j.set("band", Json::of(c.result.band));
    j.set("likelyPipe", Json::of(c.result.likelyPipe));
    j.set("bigPipe", Json::of(c.result.bigPipe));
    j.set("drugUntil", Json::of(clockText(c.result.drugUntil)));
    j.set("drugPossible", Json::of(c.result.drugPossible));
    j.set("clotUntil", Json::of(clockText(c.result.clotUntil)));
    j.set("clotPossible", Json::of(c.result.clotPossible));

    Json history = Json::array();
    for (const auto& u : c.history) {
        Json item = Json::object();
        item.set("time", Json::of(clockText(u.time)));
        item.set("addedSymptoms", Json::of(u.addedSymptoms));
        item.set("severity", Json::of(u.severity));
        item.set("likelyPipe", Json::of(u.likelyPipe));
        history.add(item);
    }
    j.set("history", history);
    if (c.routed) j.set("route", routeToJson(c.route));
    if (c.stayed) j.set("hospitalStay", stayToJson(c.stay));
    return j;
}

static bool readText(const Json& j, const std::string& key, std::string& out)
{
    const Json* v = j.get(key);
    if (!v || v->type != Json::Text) return false;
    out = v->text;
    return true;
}

static bool readInt(const Json& j, const std::string& key, int& out)
{
    const Json* v = j.get(key);
    if (!v || v->type != Json::Number) return false;
    out = (int)v->number;
    return true;
}

static bool readBool(const Json& j, const std::string& key, bool& out)
{
    const Json* v = j.get(key);
    if (!v || v->type != Json::Bool) return false;
    out = v->boolean;
    return true;
}

static bool readClock(const Json& j, const std::string& key, int& out)
{
    std::string text;
    return readText(j, key, text) && parseClock(text, out);
}

static bool readList(const Json& j, const std::string& key, std::vector<std::string>& out)
{
    const Json* v = j.get(key);
    if (!v || v->type != Json::Array) return false;
    for (const auto& item : v->items) {
        if (item.type != Json::Text) return false;
        out.push_back(item.text);
    }
    return true;
}

// Reads saved route times, which can say "tomorrow" past midnight.
static bool readMoment(const Json& j, const std::string& key, int& out)
{
    const Json* v = j.get(key);
    if (v && v->type == Json::Null) {
        out = NO_TIME;
        return true;
    }
    std::string text;
    if (!readText(j, key, text) || !parseClock(text.substr(0, 5), out)) return false;
    if (text.find("tomorrow") != std::string::npos) out += 24 * 60;
    return true;
}

static bool routeFromJson(const Json& j, CaseRoute& r)
{
    if (!readText(j, "treatment", r.treatment) || !readText(j, "hospital", r.hospital) ||
        !readList(j, "path", r.path) || !readInt(j, "driveMinutes", r.driveMinutes) ||
        !readMoment(j, "leaves", r.leaves) || !readMoment(j, "arrives", r.arrives) ||
        !readMoment(j, "drugAt", r.drugAt) || !readMoment(j, "clotRemovalAt", r.clotAt) ||
        !readBool(j, "transfer", r.transfer))
        return false;
    if (!r.transfer) return true;
    return readText(j, "firstHospital", r.firstHospital) && readList(j, "firstPath", r.firstPath) &&
           readInt(j, "firstDriveMinutes", r.firstDriveMinutes) &&
           readMoment(j, "firstLeaves", r.firstLeaves) &&
           readMoment(j, "firstArrives", r.firstArrives);
}

static bool stayFromJson(const Json& j, HospitalStay& s)
{
    return readText(j, "hospital", s.hospital) && readMoment(j, "arrives", s.arrives) &&
           readMoment(j, "scanAt", s.scanAt) && readMoment(j, "drugAt", s.drugAt) &&
           readMoment(j, "clotRemovalAt", s.clotAt);
}

static bool fromJson(const Json& j, Case& c)
{
    int clock = 0;
    bool yesterday = false;
    if (!readText(j, "id", c.id) || !readText(j, "name", c.name) || !readInt(j, "age", c.age) ||
        !readText(j, "city", c.city) || !readClock(j, "createdAt", c.createdAt) ||
        !readClock(j, "timeNow", c.timeNow) || !readClock(j, "lastSeenWell", clock) ||
        !readBool(j, "lastSeenWellYesterday", yesterday) || !readList(j, "symptoms", c.symptoms) ||
        !readInt(j, "severity", c.result.severity) || !readText(j, "band", c.result.band) ||
        !readText(j, "likelyPipe", c.result.likelyPipe) || !readBool(j, "bigPipe", c.result.bigPipe) ||
        !readBool(j, "drugPossible", c.result.drugPossible) ||
        !readBool(j, "clotPossible", c.result.clotPossible))
        return false;

    // The limits are saved as text for people; the numbers come back from the rules.
    c.lastSeen = lastSeenMinutes(clock, yesterday);
    c.result.drugUntil = drugLimit(c.lastSeen);
    c.result.clotUntil = clotLimit(c.lastSeen);

    const Json* history = j.get("history");
    if (!history || history->type != Json::Array) return false;
    for (const auto& item : history->items) {
        CaseUpdate u;
        if (!readClock(item, "time", u.time) || !readList(item, "addedSymptoms", u.addedSymptoms) ||
            !readInt(item, "severity", u.severity) || !readText(item, "likelyPipe", u.likelyPipe))
            return false;
        c.history.push_back(u);
    }

    const Json* route = j.get("route");
    if (route) {
        if (!routeFromJson(*route, c.route)) return false;
        c.routed = true;
    }

    const Json* stay = j.get("hospitalStay");
    if (stay) {
        if (!stayFromJson(*stay, c.stay)) return false;
        c.stayed = true;
    }
    return true;
}

bool CaseStore::load(const std::string& path)
{
    path_ = path;
    cases_.clear();
    nextId_ = 1;
    damaged_ = false;

    std::ifstream f(path);
    if (!f) return true;
    std::stringstream content;
    content << f.rdbuf();

    Json root;
    const Json* list = nullptr;
    damaged_ = !Json::parse(content.str(), root) || !(list = root.get("cases")) ||
               list->type != Json::Array || !readInt(root, "nextId", nextId_);
    if (damaged_) return false;

    for (const auto& item : list->items) {
        Case c;
        if (!fromJson(item, c)) {
            cases_.clear();
            damaged_ = true;
            return false;
        }
        nextId_ = std::max(nextId_, idNumber(c.id) + 1);
        cases_.push_back(c);
    }
    return true;
}

// Written to a second file first, so a failed write cannot damage the database.
bool CaseStore::save() const
{
    if (damaged_) return false;

    Json list = Json::array();
    for (const auto& c : cases_) list.add(toJson(c));
    Json root = Json::object();
    root.set("nextId", Json::of(nextId_));
    root.set("cases", list);

    std::string temporary = path_ + ".tmp";
    {
        std::ofstream f(temporary);
        if (!f) return false;
        f << root.dump() << "\n";
        if (!f) return false;
    }
    std::remove(path_.c_str());
    return std::rename(temporary.c_str(), path_.c_str()) == 0;
}

bool CaseStore::add(Case& c)
{
    c.id = formatId(nextId_);
    cases_.push_back(c);
    ++nextId_;
    if (save()) return true;
    cases_.pop_back();
    --nextId_;
    return false;
}

bool CaseStore::update(const Case& c)
{
    for (auto& existing : cases_)
        if (existing.id == c.id) {
            Case before = existing;
            existing = c;
            if (save()) return true;
            existing = before;
            return false;
        }
    return false;
}

const Case* CaseStore::find(const std::string& id) const
{
    for (const auto& c : cases_)
        if (c.id == id) return &c;
    return nullptr;
}
