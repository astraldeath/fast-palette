#include "core.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>

namespace palette {

namespace {

constexpr std::size_t kMaximumExpressionLength = 512;
constexpr unsigned kMaximumNesting = 64;

enum class ParseIssue { none, incomplete, invalid, mathematical };

class Parser {
public:
    explicit Parser(std::wstring_view input) : input_(input) {}

    double parse() {
        const double result = additive();
        skip_space();
        if (issue_ == ParseIssue::none && position_ != input_.size()) {
            issue_ = ParseIssue::invalid;
        }
        return result;
    }

    ParseIssue issue() const { return issue_; }

private:
    void skip_space() {
        while (position_ < input_.size() && iswspace(input_[position_])) {
            ++position_;
        }
    }

    bool consume(wchar_t character) {
        skip_space();
        if (position_ < input_.size() && input_[position_] == character) {
            ++position_;
            return true;
        }
        return false;
    }

    void finite_or_error(double value) {
        if (issue_ == ParseIssue::none && !std::isfinite(value)) {
            issue_ = ParseIssue::mathematical;
        }
    }

    double additive() {
        double left = multiplicative();
        while (issue_ == ParseIssue::none) {
            if (consume(L'+')) {
                left += multiplicative();
            } else if (consume(L'-')) {
                left -= multiplicative();
            } else {
                break;
            }
            finite_or_error(left);
        }
        return left;
    }

    double multiplicative() {
        double left = unary();
        while (issue_ == ParseIssue::none) {
            if (consume(L'*')) {
                left *= unary();
                finite_or_error(left);
            } else if (consume(L'/')) {
                const double divisor = unary();
                if (issue_ != ParseIssue::none) {
                    break;
                }
                if (divisor == 0.0) {
                    issue_ = ParseIssue::mathematical;
                    break;
                }
                left /= divisor;
                finite_or_error(left);
            } else {
                break;
            }
        }
        return left;
    }

    double unary() {
        if (consume(L'+')) {
            return unary();
        }
        if (consume(L'-')) {
            return -unary();
        }
        return power();
    }

    double power() {
        double base = postfix();
        if (issue_ == ParseIssue::none && consume(L'^')) {
            const double exponent = unary();
            if (issue_ == ParseIssue::none) {
                base = std::pow(base, exponent);
                finite_or_error(base);
            }
        }
        return base;
    }

    double postfix() {
        double value = primary();
        while (issue_ == ParseIssue::none) {
            if (consume(L'%')) {
                value /= 100.0;
            } else if (consume(L'!')) {
                // Repeated bangs are ambiguous with double-factorial notation.
                if (consume(L'!')) {
                    issue_ = ParseIssue::invalid;
                    break;
                }
                if (!std::isfinite(value) || value < 0.0 || value > 170.0 || std::trunc(value) != value) {
                    issue_ = ParseIssue::mathematical;
                    break;
                }
                const unsigned operand = static_cast<unsigned>(value);
                value = 1.0;
                for (unsigned factor = 2; factor <= operand; ++factor) value *= factor;
            } else {
                break;
            }
        }
        return value;
    }

    double primary() {
        skip_space();
        if (position_ == input_.size()) {
            issue_ = ParseIssue::incomplete;
            return 0.0;
        }

        if (consume(L'(')) {
            if (++nesting_ > kMaximumNesting) {
                issue_ = ParseIssue::invalid;
                return 0.0;
            }
            const double value = additive();
            if (issue_ == ParseIssue::none && !consume(L')')) {
                issue_ = position_ == input_.size() ? ParseIssue::incomplete : ParseIssue::invalid;
            }
            --nesting_;
            return value;
        }

        if (iswdigit(input_[position_]) || input_[position_] == L'.') {
            return number();
        }
        if (iswalpha(input_[position_])) {
            return identifier();
        }

        issue_ = ParseIssue::invalid;
        return 0.0;
    }

    double number() {
        const std::size_t start = position_;
        bool digits = false;
        while (position_ < input_.size() && iswdigit(input_[position_])) {
            digits = true;
            ++position_;
        }
        if (position_ < input_.size() && input_[position_] == L'.') {
            ++position_;
            while (position_ < input_.size() && iswdigit(input_[position_])) {
                digits = true;
                ++position_;
            }
        }
        if (!digits) {
            issue_ = ParseIssue::invalid;
            return 0.0;
        }
        if (position_ < input_.size() && (input_[position_] == L'e' || input_[position_] == L'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == L'+' || input_[position_] == L'-')) {
                ++position_;
            }
            const std::size_t exponent_start = position_;
            while (position_ < input_.size() && iswdigit(input_[position_])) {
                ++position_;
            }
            if (position_ == exponent_start) {
                issue_ = position_ == input_.size() ? ParseIssue::incomplete : ParseIssue::invalid;
                return 0.0;
            }
        }

        const std::wstring token(input_.substr(start, position_ - start));
        wchar_t* end = nullptr;
        const double value = std::wcstod(token.c_str(), &end);
        if (!end || *end != L'\0' || !std::isfinite(value)) {
            issue_ = ParseIssue::mathematical;
        }
        return value;
    }

    double identifier() {
        const std::size_t start = position_;
        while (position_ < input_.size() && iswalnum(input_[position_])) {
            ++position_;
        }
        std::wstring name(input_.substr(start, position_ - start));
        for (wchar_t& character : name) {
            character = static_cast<wchar_t>(towlower(character));
        }
        if (name == L"pi") {
            return std::numbers::pi_v<double>;
        }
        if (name == L"e") {
            return std::numbers::e_v<double>;
        }

        const bool known = name == L"sqrt" || name == L"abs" || name == L"sin" ||
                           name == L"cos" || name == L"tan" || name == L"log10" ||
                           name == L"ln";
        if (!known) {
            issue_ = ParseIssue::invalid;
            return 0.0;
        }
        if (!consume(L'(')) {
            issue_ = position_ == input_.size() ? ParseIssue::incomplete : ParseIssue::invalid;
            return 0.0;
        }
        if (++nesting_ > kMaximumNesting) {
            issue_ = ParseIssue::invalid;
            return 0.0;
        }
        const double argument = additive();
        if (issue_ == ParseIssue::none && !consume(L')')) {
            issue_ = position_ == input_.size() ? ParseIssue::incomplete : ParseIssue::invalid;
        }
        --nesting_;
        if (issue_ != ParseIssue::none) {
            return 0.0;
        }

        double value = 0.0;
        if (name == L"sqrt") value = std::sqrt(argument);
        else if (name == L"abs") value = std::abs(argument);
        else if (name == L"sin") value = std::sin(argument);
        else if (name == L"cos") value = std::cos(argument);
        else if (name == L"tan") value = std::tan(argument);
        else if (name == L"log10") value = std::log10(argument);
        else value = std::log(argument);
        finite_or_error(value);
        return value;
    }

    std::wstring_view input_;
    std::size_t position_ = 0;
    unsigned nesting_ = 0;
    ParseIssue issue_ = ParseIssue::none;
};

bool starts_as_calculation(std::wstring_view input) {
    std::size_t position = 0;
    while (position < input.size() && iswspace(input[position])) ++position;
    if (position == input.size()) return false;
    const wchar_t first = input[position];
    if (iswdigit(first) || first == L'.' || first == L'+' || first == L'-' || first == L'(') {
        return true;
    }
    if (!iswalpha(first)) return false;

    const std::size_t start = position;
    while (position < input.size() && iswalnum(input[position])) ++position;
    std::wstring name(input.substr(start, position - start));
    for (wchar_t& character : name) character = static_cast<wchar_t>(towlower(character));
    if (name == L"pi" || name == L"e") return true;
    while (position < input.size() && iswspace(input[position])) ++position;
    const bool function = name == L"sqrt" || name == L"abs" || name == L"sin" ||
                          name == L"cos" || name == L"tan" || name == L"log10" || name == L"ln";
    return function && (position == input.size() || input[position] == L'(');
}

std::wstring format_value(double value) {
    if (value == 0.0) value = 0.0;
    char buffer[128]{};
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
    if (result.ec != std::errc{}) return {};
    std::string text(buffer, result.ptr);
    if (text.starts_with("0.")) text.erase(0, 1);
    else if (text.starts_with("-0.")) text.erase(1, 1);
    return std::wstring(text.begin(), text.end());
}

} // namespace

CalcResult calculate(std::wstring_view expression) {
    bool forced = false;
    std::size_t first = 0;
    while (first < expression.size() && iswspace(expression[first])) ++first;
    if (first < expression.size() && expression[first] == L'=') {
        forced = true;
        expression.remove_prefix(first + 1);
    }

    if (expression.size() > kMaximumExpressionLength) {
        return {CalcStatus::error, 0.0, L"Expression is too long"};
    }
    if (!forced && !starts_as_calculation(expression)) {
        return {};
    }

    Parser parser(expression);
    const double value = parser.parse();
    switch (parser.issue()) {
    case ParseIssue::none:
        return {CalcStatus::value, value, format_value(value)};
    case ParseIssue::incomplete:
        return {CalcStatus::incomplete, 0.0, {}};
    case ParseIssue::mathematical:
        return {CalcStatus::error, 0.0, L"Mathematical error"};
    case ParseIssue::invalid:
        return {CalcStatus::error, 0.0, L"Invalid expression"};
    }
    return {CalcStatus::error, 0.0, L"Invalid expression"};
}

} // namespace palette
