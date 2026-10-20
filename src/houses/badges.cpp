#include "houses/badges.hpp"

#include <algorithm>

namespace wcatalog {

namespace {

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) {
        s.replace(pos, from.size(), to);
    }
    return s;
}

}  // namespace

std::vector<Badge> resolve_badges(const std::vector<Feature>& catalog, const std::map<std::int64_t, std::string>& values) {
    std::vector<const Feature*> ordered;
    for (const Feature& f : catalog) ordered.push_back(&f);
    std::stable_sort(ordered.begin(), ordered.end(), [](const Feature* a, const Feature* b) {
        if (a->order != b->order) return a->order < b->order;
        return a->id < b->id;
    });

    std::vector<Badge> out;
    for (const Feature* f : ordered) {
        if (!f->active || !f->badge_visible) continue;
        auto it = values.find(f->id);
        if (it == values.end() || it->second.empty()) continue;
        if (f->type == "bool" && it->second != "1") continue;

        const std::string shown = display_feature_value(*f, it->second);
        if (shown.empty()) continue;

        Badge b;
        b.feature_id = f->id;
        b.role = f->badge_role;
        b.icon = f->icon;
        if (f->badge_text && !f->badge_text->empty()) {
            b.text = replace_all(*f->badge_text, "{valor}", shown);
        } else if (f->type == "bool") {
            b.text = f->label;
        } else {
            b.text = f->label + ": " + shown;
        }
        out.push_back(std::move(b));
    }
    return out;
}

Json badges_to_json(const std::vector<Badge>& badges) {
    Json arr = Json::array();
    for (const Badge& b : badges) {
        arr.push_back(Json{{"caracteristica_id", b.feature_id}, {"texto", b.text}, {"rol", b.role}, {"icono", b.icon}});
    }
    return arr;
}

}  // namespace wcatalog
