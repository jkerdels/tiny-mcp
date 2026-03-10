#include "notes.h"

void NoteStore::add(const std::string& title, const std::string& body) {
    notes_[title] = body;
}

std::optional<std::string> NoteStore::get(const std::string& title) const {
    auto it = notes_.find(title);
    if (it == notes_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::string> NoteStore::list() const {
    std::vector<std::string> titles;
    titles.reserve(notes_.size());
    for (const auto& [title, body] : notes_) {
        titles.push_back(title);
    }
    return titles;
}
