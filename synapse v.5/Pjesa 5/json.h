#ifndef JSON_H
#define JSON_H

#include <string>
#include <vector>

// A small JSON value: enough to read and write the case database.
struct Json
{
    enum Type { Null, Bool, Number, Text, Array, Object };

    Type type = Null;
    bool boolean = false;
    long number = 0;
    std::string text;
    std::vector<std::string> keys;   // for an object, one key per item
    std::vector<Json> items;

    static Json of(bool value);
    static Json of(int value);
    static Json of(const std::string& value);
    static Json of(const std::vector<std::string>& values);
    static Json array();
    static Json object();

    void add(const Json& item);                                // array
    void set(const std::string& key, const Json& value);      // object
    const Json* get(const std::string& key) const;

    std::string dump(int depth = 0) const;
    static bool parse(const std::string& source, Json& out);
};

#endif // JSON_H
