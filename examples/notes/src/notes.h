#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

class NoteStore {
public:
    void add(const std::string& title, const std::string& body);
    std::optional<std::string> get(const std::string& title) const;
    std::vector<std::string> list() const;

private:
    std::map<std::string, std::string> notes_;
};
