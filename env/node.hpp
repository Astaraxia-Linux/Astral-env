#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>
#include <optional>

namespace env {

struct Node;

using NodeMap  = std::map<std::string, Node>;
using NodeList = std::vector<Node>;

struct Node {
    std::variant<std::string, NodeList, NodeMap> value;

    Node() : value(NodeMap{}) {}
    explicit Node(std::string s)  : value(std::move(s))  {}
    explicit Node(NodeList l)     : value(std::move(l))  {}
    explicit Node(NodeMap m)      : value(std::move(m))  {}

    bool is_str()  const { return std::holds_alternative<std::string>(value); }
    bool is_list() const { return std::holds_alternative<NodeList>(value);    }
    bool is_map()  const { return std::holds_alternative<NodeMap>(value);     }

    const std::string& str()   const { return std::get<std::string>(value); }
    const NodeList&    list()  const { return std::get<NodeList>(value);    }
    const NodeMap&     map()   const { return std::get<NodeMap>(value);     }
    NodeMap&           map()         { return std::get<NodeMap>(value);     }

    std::optional<const Node*> get(std::string_view key) const {
        if (!is_map()) return std::nullopt;
        auto it = map().find(std::string(key));
        if (it == map().end()) return std::nullopt;
        return &it->second;
    }

    // Walk a dot-separated path: get_path("System.hostname")
    std::optional<const Node*> get_path(std::string_view path) const {
        const Node* cur = this;
        std::string_view rem = path;
        while (!rem.empty()) {
            auto dot = rem.find('.');
            auto key = (dot == std::string_view::npos) ? rem : rem.substr(0, dot);
            auto next = cur->get(key);
            if (!next) return std::nullopt;
            cur = *next;
            rem = (dot == std::string_view::npos) ? "" : rem.substr(dot + 1);
        }
        return cur;
    }

    std::string str_or(std::string_view def) const {
        return is_str() ? str() : std::string(def);
    }
};

// Recursively merge src into dst. src values win on scalar conflicts.
void merge(NodeMap& dst, const NodeMap& src);

} // namespace env
