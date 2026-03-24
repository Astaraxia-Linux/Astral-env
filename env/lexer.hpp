#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>

namespace env {

enum class TokKind {
    Ident,       // bare identifier or dotted key segment
    String,      // "quoted value"
    Eq,          // =
    Colon,       // :
    LBrace,      // {
    RBrace,      // }
    LBracket,    // [
    RBracket,    // ]
    Comma,       // ,
    Semicolon,   // ;
    Dollar,      // $ (start of $ENV)
    Eof,
};

struct Token {
    TokKind     kind;
    std::string text;
    int         line;
};

class LexError : public std::runtime_error {
public:
    LexError(int line, std::string_view msg)
        : std::runtime_error("line " + std::to_string(line) + ": " + std::string(msg)) {}
};

std::vector<Token> lex(std::string_view src, std::string_view filename = "<input>");

} // namespace env
