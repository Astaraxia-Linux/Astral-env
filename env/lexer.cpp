#include "env/lexer.hpp"

#include <cctype>
#include <string>

namespace env {

namespace {

struct Cursor {
    std::string_view src;
    std::size_t      pos  = 0;
    int              line = 1;
    std::string_view file;

    char peek(std::size_t off = 0) const {
        return (pos + off < src.size()) ? src[pos + off] : '\0';
    }
    char get() {
        char c = src[pos++];
        if (c == '\n') ++line;
        return c;
    }
    bool at_end() const { return pos >= src.size(); }
};

// Strip #/ ... /# multiline comments in-place before tokenising.
// Replaces comment content with spaces to preserve line numbers.
std::string strip_multiline(std::string_view src) {
    std::string out(src);
    std::size_t i = 0;
    while (i + 1 < out.size()) {
        if (out[i] == '#' && out[i + 1] == '/') {
            out[i] = ' '; out[i + 1] = ' ';
            i += 2;
            while (i + 1 < out.size()) {
                if (out[i] == '/' && out[i + 1] == '#') {
                    out[i] = ' '; out[i + 1] = ' ';
                    i += 2;
                    break;
                }
                if (out[i] != '\n') out[i] = ' ';
                ++i;
            }
        } else {
            ++i;
        }
    }
    return out;
}

void skip_line_comment(Cursor& c) {
    while (!c.at_end() && c.peek() != '\n') c.get();
}

std::string lex_string(Cursor& c) {
    c.get(); // consume opening "
    std::string val;
    while (!c.at_end()) {
        char ch = c.get();
        if (ch == '"') return val;
        if (ch == '\\') {
            if (c.at_end()) break;
            char esc = c.get();
            switch (esc) {
                case 'n':  val += '\n'; break;
                case 't':  val += '\t'; break;
                case '"':  val += '"';  break;
                case '\\': val += '\\'; break;
                default:   val += '\\'; val += esc;
            }
        } else {
            val += ch;
        }
    }
    throw LexError(c.line, "unterminated string literal");
}

// Ident: letter/digit/underscore/-/. but stops at operator chars
std::string lex_ident(Cursor& c) {
    std::string id;
    while (!c.at_end()) {
        char ch = c.peek();
        if (std::isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '.') {
            id += c.get();
        } else {
            break;
        }
    }
    return id;
}

} // anonymous namespace

std::vector<Token> lex(std::string_view src, std::string_view filename) {
    std::string cleaned = strip_multiline(src);
    Cursor c{ cleaned, 0, 1, filename };
    std::vector<Token> tokens;

    while (!c.at_end()) {
        char ch = c.peek();

        // Whitespace
        if (std::isspace((unsigned char)ch)) { c.get(); continue; }

        // Line comment
        if (ch == '#') { c.get(); skip_line_comment(c); continue; }

        int tok_line = c.line;

        switch (ch) {
            case '=': c.get(); tokens.push_back({TokKind::Eq,        "=", tok_line}); break;
            case ':': c.get(); tokens.push_back({TokKind::Colon,      ":", tok_line}); break;
            case '{': c.get(); tokens.push_back({TokKind::LBrace,     "{", tok_line}); break;
            case '}': c.get(); tokens.push_back({TokKind::RBrace,     "}", tok_line}); break;
            case '[': c.get(); tokens.push_back({TokKind::LBracket,   "[", tok_line}); break;
            case ']': c.get(); tokens.push_back({TokKind::RBracket,   "]", tok_line}); break;
            case ',': c.get(); tokens.push_back({TokKind::Comma,       ",", tok_line}); break;
            case ';': c.get(); tokens.push_back({TokKind::Semicolon,  ";", tok_line}); break;
            case '$': c.get(); tokens.push_back({TokKind::Dollar,     "$", tok_line}); break;
            case '"': {
                std::string s = lex_string(c);
                tokens.push_back({TokKind::String, std::move(s), tok_line});
                break;
            }
            default: {
                if (std::isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '/') {
                    std::string id = lex_ident(c);
                    tokens.push_back({TokKind::Ident, std::move(id), tok_line});
                } else {
                    c.get(); // skip unknown
                }
                break;
            }
        }
    }

    tokens.push_back({TokKind::Eof, "", c.line});
    return tokens;
}

} // namespace env
