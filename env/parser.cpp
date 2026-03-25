#include "env/parser.hpp"
#include "env/lexer.hpp"
#include "util/file.hpp"

#include <cassert>
#include <span>

namespace env {

namespace {

struct Parser {
    std::span<const Token> toks;
    std::size_t            pos = 0;
    std::filesystem::path  base_dir;

    const Token& peek(std::size_t off = 0) const {
        std::size_t i = pos + off;
        return (i < toks.size()) ? toks[i] : toks.back(); // back == Eof
    }
    const Token& get() { return toks[pos < toks.size() ? pos++ : pos]; }

    bool at(TokKind k, std::size_t off = 0) const { return peek(off).kind == k; }

    const Token& expect(TokKind k, std::string_view what) {
        if (!at(k)) {
            throw ParseError(peek().line,
                std::string("expected ") + std::string(what) +
                ", got '" + peek().text + "'");
        }
        return get();
    }

    void skip_semicolons() {
        while (at(TokKind::Semicolon)) get();
    }

    // Insert a value at a dot-separated path into a NodeMap.
    // "A.B.C" with value V becomes map["A"]["B"]["C"] = V.
    void insert_dotpath(NodeMap& m, const std::string& dotkey, Node val) {
        auto dot = dotkey.find('.');
        if (dot == std::string::npos) {
            auto it = m.find(dotkey);
            if (it != m.end() && it->second.is_map() && val.is_map()) {
                merge(it->second.map(), val.map());
            } else {
                m[dotkey] = std::move(val);
            }
            return;
        }
        std::string head = dotkey.substr(0, dot);
        std::string tail = dotkey.substr(dot + 1);
        if (!m.count(head) || !m[head].is_map())
            m[head] = Node(NodeMap{});
        insert_dotpath(m[head].map(), tail, std::move(val));
    }

    // Parse a [ list ] of strings
    Node parse_list() {
        expect(TokKind::LBracket, "'['");
        NodeList items;
        while (!at(TokKind::RBracket) && !at(TokKind::Eof)) {
            if (at(TokKind::String) || at(TokKind::Ident)) {
                items.emplace_back(get().text);
            } else if (at(TokKind::Comma) || at(TokKind::Semicolon)) {
                get();
            } else {
                get(); // skip unknown
            }
        }
        expect(TokKind::RBracket, "']'");
        return Node(std::move(items));
    }

    // Parse a { block } — returns a NodeMap Node
    Node parse_block() {
        expect(TokKind::LBrace, "'{'");
        NodeMap m;
        while (!at(TokKind::RBrace) && !at(TokKind::Eof)) {
            skip_semicolons();
            if (at(TokKind::RBrace)) break;
            parse_entry(m);
            skip_semicolons();
        }
        expect(TokKind::RBrace, "'}'");
        return Node(std::move(m));
    }

    // Parse one key (= value | : block | : [ list ] | bare-ident-in-list-context)
    void parse_entry(NodeMap& m) {
        // Bare string value (package name without a key)
        if (at(TokKind::String) && !at(TokKind::Eq, 1) && !at(TokKind::Colon, 1)) {
            std::string val = get().text;
            // Store unnamed values under auto-index key
            std::string key = "__item_" + std::to_string(m.size());
            m[key] = Node(val);
            return;
        }

        // Key: either Ident (possibly dotted) or String
        std::string key;
        if (at(TokKind::Ident)) {
            key = get().text;
        } else if (at(TokKind::String)) {
            key = get().text;
        } else if (at(TokKind::Dollar)) {
            // $ENV or $ENV.Something — consume $, then ident (dots are eaten by lexer,
            // so sub-keys arrive as plain Ident tokens).
            get(); // $
            if (!at(TokKind::Ident)) throw ParseError(peek().line, "expected identifier after '$'");
            get(); // ENV — consume and discard the header name
            // Collect remaining ident segments as the actual dotpath key
            key = "";
            while (at(TokKind::Ident) &&
                   !at(TokKind::Colon) && !at(TokKind::Eq) &&
                   !at(TokKind::LBrace) && !at(TokKind::Semicolon)) {
                if (!key.empty()) key += ".";
                key += get().text;
            }
            if (key.empty()) {
                // bare $ENV with no subpath — skip this entry
                skip_semicolons();
                return;
            }
        } else {
            // Skip unparseable token
            get();
            return;
        }

        if (at(TokKind::Colon)) {
            get();
            skip_semicolons();
            Node val;
            if (at(TokKind::LBrace)) {
                val = parse_block();
            } else if (at(TokKind::LBracket)) {
                val = parse_list();
            } else {
                val = Node(NodeMap{}); // empty block
            }
            insert_dotpath(m, key, std::move(val));
        } else if (at(TokKind::Eq)) {
            get();
            Node val;
            if (at(TokKind::LBracket)) {
                val = parse_list();
            } else if (at(TokKind::String)) {
                val = Node(get().text);
            } else if (at(TokKind::Ident)) {
                val = Node(get().text);
            } else {
                val = Node(std::string{});
            }
            insert_dotpath(m, key, std::move(val));
        } else {
            // Bare identifier — treat as list-item (package name)
            std::string item_key = "__item_" + std::to_string(m.size());
            m[item_key] = Node(key);
        }

        skip_semicolons();
    }

    // Handle Includes block by loading and merging referenced files
    void process_includes(NodeMap& root) {
        auto it = root.find("Includes");
        if (it == root.end() || !it->second.is_map()) return;

        for (const auto& [k, v] : it->second.map()) {
            if (!v.is_str()) continue;
            std::string path_str = v.str();
            if (path_str.empty()) continue;

            // Resolve relative path
            auto inc_path = base_dir / path_str;
            if (!std::filesystem::exists(inc_path)) continue;

            try {
                NodeMap inc = parse_file_impl(inc_path);
                merge(root, inc);
            } catch (const std::exception& e) {
                // Non-fatal: report but continue
                std::fprintf(stderr, "warning: Includes '%s': %s\n",
                    inc_path.c_str(), e.what());
            }
        }
        root.erase("Includes");
    }

    NodeMap parse_file_impl(const std::filesystem::path& path) {
        std::string src = util::read_file(path);
        auto saved_base = base_dir;
        base_dir = path.parent_path();
        auto toks_vec = lex(src, path.string());
        auto saved_toks = toks;
        auto saved_pos  = pos;
        toks = std::span<const Token>(toks_vec);
        pos  = 0;

        NodeMap result = parse_root();

        toks     = saved_toks;
        pos      = saved_pos;
        base_dir = saved_base;
        return result;
    }

    // Top-level: consume $ENV: { ... } or bare { ... }
    NodeMap parse_root() {
        NodeMap root;

        // Skip leading $ENV or $ENV.Something header
        if (at(TokKind::Dollar)) {
            get(); // $
            if (at(TokKind::Ident)) get(); // ENV
            // optional .Sub — skip until : or {
            while (at(TokKind::Ident) && !at(TokKind::Colon) && !at(TokKind::LBrace))
                get();
        }

        if (at(TokKind::Colon)) get();
        skip_semicolons();

        if (at(TokKind::LBrace)) {
            Node block = parse_block();
            if (block.is_map()) root = block.map();
        } else {
            // Try parsing as a flat sequence of entries
            while (!at(TokKind::Eof)) {
                skip_semicolons();
                if (at(TokKind::Eof)) break;
                parse_entry(root);
            }
        }

        process_includes(root);
        return root;
    }
};

} // anonymous namespace

NodeMap parse_str(std::string_view src, std::string_view name,
                  const std::filesystem::path& base_dir) {
    auto toks_vec = lex(src, name);
    Parser p;
    p.toks     = std::span<const Token>(toks_vec);
    p.base_dir = base_dir;
    return p.parse_root();
}

NodeMap parse_file(const std::filesystem::path& path) {
    std::string src = util::read_file(path);
    auto toks_vec = lex(src, path.string());
    Parser p;
    p.toks     = std::span<const Token>(toks_vec);
    p.base_dir = path.parent_path();
    return p.parse_root();
}

} // namespace env
