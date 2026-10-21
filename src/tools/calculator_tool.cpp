#include "calculator_tool.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace edu_schedule {
namespace {

constexpr std::size_t kMaximumInputLength = 256;
constexpr std::size_t kMaximumRecursionDepth = 64;

constexpr double kMinimumCompensationFactor = 0.75;
constexpr double kMaximumCompensationFactor = 1.50;

class ParseError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ExpressionParser {
public:
    explicit ExpressionParser(std::string_view input)
        : input_(input) {
    }

    double parse() {
        skipWhitespace();

        if (atEnd()) {
            throw ParseError("expression is empty");
        }

        const double result = parseExpression();

        skipWhitespace();

        if (!atEnd()) {
            throw ParseError(
                "unexpected token at position "
                + std::to_string(position_)
            );
        }

        return requireFinite(result);
    }

private:
    class RecursionGuard {
    public:
        explicit RecursionGuard(std::size_t& depth)
            : depth_(depth) {
            ++depth_;

            if (depth_ > kMaximumRecursionDepth) {
                --depth_;
                throw ParseError("expression is nested too deeply");
            }
        }

        ~RecursionGuard() {
            --depth_;
        }

    private:
        std::size_t& depth_;
    };

    double parseExpression() {
        double result = parseTerm();

        while (true) {
            if (match('+')) {
                result = requireFinite(result + parseTerm());
            } else if (match('-')) {
                result = requireFinite(result - parseTerm());
            } else {
                return result;
            }
        }
    }

    double parseTerm() {
        double result = parseUnary();

        while (true) {
            if (match('*')) {
                result = requireFinite(result * parseUnary());
            } else if (match('/')) {
                const double divisor = parseUnary();

                if (divisor == 0.0) {
                    throw ParseError("division by zero");
                }

                result = requireFinite(result / divisor);
            } else if (match('%')) {
                const double divisor = parseUnary();

                if (divisor == 0.0) {
                    throw ParseError("modulo by zero");
                }

                result = requireFinite(std::fmod(result, divisor));
            } else {
                return result;
            }
        }
    }

    double parseUnary() {
        RecursionGuard guard(recursionDepth_);

        if (match('+')) {
            return parseUnary();
        }

        if (match('-')) {
            return requireFinite(-parseUnary());
        }

        return parsePower();
    }

    double parsePower() {
        double result = parsePrimary();

        if (match('^')) {
            result = requireFinite(
                std::pow(result, parseUnary())
            );
        }

        return result;
    }

    double parsePrimary() {
        if (match('(')) {
            const double result = parseExpression();

            require(
                ')',
                "expected ')' at position "
            );

            return result;
        }

        skipWhitespace();

        if (!atEnd()) {
            const unsigned char character =
                static_cast<unsigned char>(input_[position_]);

            if (std::isalpha(character)
                || input_[position_] == '_') {
                return parseFunction();
            }
        }

        return parseNumber();
    }

    double parseFunction() {
        const std::string_view identifier = parseIdentifier();

        require(
            '(',
            "expected '(' after function name at position "
        );

        const double firstArgument = parseExpression();

        require(
            ',',
            "expected ',' in function call at position "
        );

        const double secondArgument = parseExpression();

        require(
            ')',
            "expected ')' after function arguments at position "
        );

        if (identifier == "compensation_factor") {
            if (firstArgument <= 0.0 || secondArgument <= 0.0) {
                throw ParseError(
                    "estimated and actual minutes must be positive"
                );
            }

            const double rawFactor = requireFinite(
                secondArgument / firstArgument
            );

            return std::clamp(
                rawFactor,
                kMinimumCompensationFactor,
                kMaximumCompensationFactor
            );
        }

        if (identifier == "adjusted_minutes") {
            if (firstArgument < 0.0 || secondArgument <= 0.0) {
                throw ParseError(
                    "base minutes must be non-negative "
                    "and factor must be positive"
                );
            }

            return requireFinite(
                std::ceil(firstArgument * secondArgument)
            );
        }

        throw ParseError(
            "unknown function: " + std::string(identifier)
        );
    }

    double parseNumber() {
        skipWhitespace();

        if (atEnd()) {
            throw ParseError(
                "expected a number at position "
                + std::to_string(position_)
            );
        }

        const char* begin = input_.data() + position_;
        const char* end = input_.data() + input_.size();

        double value = 0.0;

        const auto [next, error] = std::from_chars(
            begin,
            end,
            value,
            std::chars_format::general
        );

        if (error != std::errc{} || next == begin) {
            throw ParseError(
                "expected a number at position "
                + std::to_string(position_)
            );
        }

        position_ = static_cast<std::size_t>(
            next - input_.data()
        );

        return requireFinite(value);
    }

    std::string_view parseIdentifier() {
        skipWhitespace();

        const std::size_t begin = position_;

        while (!atEnd()) {
            const unsigned char character =
                static_cast<unsigned char>(input_[position_]);

            if (!std::isalnum(character)
                && input_[position_] != '_') {
                break;
            }

            ++position_;
        }

        return input_.substr(begin, position_ - begin);
    }

    bool match(char expected) {
        skipWhitespace();

        if (!atEnd() && input_[position_] == expected) {
            ++position_;
            return true;
        }

        return false;
    }

    void require(
        char expected,
        std::string_view errorMessage
    ) {
        if (!match(expected)) {
            throw ParseError(
                std::string(errorMessage)
                + std::to_string(position_)
            );
        }
    }

    void skipWhitespace() {
        while (!atEnd()
               && std::isspace(
                   static_cast<unsigned char>(input_[position_])
               )) {
            ++position_;
        }
    }

    [[nodiscard]]
    bool atEnd() const noexcept {
        return position_ >= input_.size();
    }

    static double requireFinite(double value) {
        if (!std::isfinite(value)) {
            throw ParseError("result is not finite");
        }

        return value;
    }

    std::string_view input_;
    std::size_t position_ = 0;
    std::size_t recursionDepth_ = 0;
};

std::string formatNumber(double value) {
    if (std::abs(value) < 1e-12) {
        value = 0.0;
    }

    const double rounded = std::round(value);
    const double tolerance =
        1e-12 * std::max(1.0, std::abs(value));

    if (std::abs(value - rounded) <= tolerance) {
        value = rounded;
    }

    std::ostringstream output;

    output
        << std::setprecision(15)
        << std::defaultfloat
        << value;

    return output.str();
}

} // namespace

std::string_view CalculatorTool::name() const noexcept {
    return "calculator";
}

std::string_view
CalculatorTool::description() const noexcept {
    return
        "Evaluate arithmetic expressions (+, -, *, /, %, ^) and "
        "study-duration functions: "
        "compensation_factor(estimated_minutes, actual_minutes) and "
        "adjusted_minutes(base_minutes, factor).";
}

ToolResult CalculatorTool::execute(std::string_view input) {
    if (input.size() > kMaximumInputLength) {
        return {
            false,
            "Calculator input is too long"
        };
    }

    try {
        ExpressionParser parser(input);

        return {
            true,
            formatNumber(parser.parse())
        };
    } catch (const ParseError& error) {
        return {
            false,
            "Invalid calculator input: "
                + std::string(error.what())
        };
    } catch (const std::exception& error) {
        return {
            false,
            "Calculator failed: "
                + std::string(error.what())
        };
    }
}

} // namespace edu_schedule
