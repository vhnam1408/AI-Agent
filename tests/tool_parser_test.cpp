#include "client/tool_parser.h"
#include "agent/ToolRegistry.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace edu_schedule;
using json = nlohmann::json;
namespace {
int checks = 0;
void check(bool ok, const char* why) {
    ++checks;
    if (!ok) throw std::runtime_error(why); // active in Release builds too
}
std::string read(const std::string& file) {
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Cannot read fixture: " + file);
    std::ostringstream s; s << input.rdbuf(); return s.str();
}
class CountingTool final : public ITool {
public:
    int executions = 0;
    std::string_view name() const noexcept override { return "task_lookup"; }
    std::string_view description() const noexcept override { return "Test only"; }
    ToolResult execute(std::string_view) override { ++executions; return {true, "ok"}; }
};
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected fixture directory");
        const std::string dir = argv[1];
        const std::vector<std::string> known{"task_lookup", "calculator"};
        auto tool = std::make_unique<CountingTool>();
        auto* counter = tool.get();
        ToolRegistry registry;
        registry.registerTool(std::move(tool));
        registry.denyTool("task_lookup");
        auto native = parse_chat_response(read(dir + "/tool_parser_native.json"), known);
        check(native && native->calls.size() == 2, "Native calls were lost");
        check(native->calls[0].tool_name == "task_lookup", "Wrong native name");
        check(json::parse(native->calls[0].arguments)["task_id"] == "T1", "Lost argument");
        check(json::parse(native->calls[1].arguments)["expression"] == "6 * 7", "String args");
        auto content = parse_chat_response(read(dir + "/tool_parser_content.json"), known);
        check(content && content->calls.size() == 1, "Content JSON not parsed");
        check(content->calls[0].arguments == native->calls[0].arguments, "Formats differ");
        for (const std::string input : {
            R"({"tool_name":"task_lookup","arguments":{}})",
            R"([{"tool_name":"task_lookup","arguments":{}},{"tool_name":"calculator","arguments":{}}])",
            R"({"tool_calls":[{"function":{"name":"task_lookup","arguments":{}}}]})",
            "```json\n{\"tool_name\":\"task_lookup\",\"arguments\":{}}\n```"}) {
            check(parse_tool_content(input, known).has_value(), "Valid content rejected");
        }
        for (const auto& input : {"", "not JSON", "null", "42", "[]", "{}",
            R"({"tool_name":"task_lookup"})",
            R"({"tool_name":"","arguments":{}})",
            R"({"tool_name":"../exec","arguments":{}})",
            R"({"tool_name":"unknown","arguments":{}})",
            R"({"tool_name":1,"arguments":{}})",
            R"({"tool_name":"task_lookup","arguments":null})",
            R"({"tool_name":"task_lookup","arguments":[]})",
            R"({"tool_name":"task_lookup","arguments":"{"})",
            R"({"tool_name":"task_lookup","arguments":"[]"})",
            R"({"tool_calls":{}})",
            R"({"tool_calls":[{}]})",
            R"({"tool_calls":[{"type":"exec","function":{"name":"task_lookup","arguments":{}}}]})",
            R"([{"tool_name":"task_lookup","arguments":{}},null])",
            R"({"tool_name":"task_lookup","arguments":{},"tool_calls":[]})"}) {
            check(!parse_tool_content(input, known), "Invalid content accepted");
        }
        for (const auto& input : {"{", "null", "[]", "{}",
            R"({"message":null})", R"({"message":{"content":3}})",
            R"({"message":{"content":null}})", R"({"message":{}})",
            R"({"message":{"content":"","tool_calls":null}})",
            R"({"done":false,"message":{"content":"ok"}})",
            R"({"done_reason":"length","message":{"content":"ok"}})",
            R"({"message":{"role":"user","content":"ok"}})",
            R"({"error":"bad","message":{"content":"ok"}})",
            R"({"message":{"content":"{bad"}})"}) {
            check(!parse_chat_response(input, known), "Invalid response accepted");
        }
        auto ambiguous = json::parse(read(dir + "/tool_parser_native.json"));
        ambiguous["message"]["content"] = json::parse(read(dir + "/tool_parser_content.json"))["message"]["content"];
        auto bad = parse_chat_response(ambiguous.dump(), known);
        check(!bad && bad.error().code == ParseErrorCode::AmbiguousCalls, "Ambiguity not rejected");
        auto no_content = json::parse(read(dir + "/tool_parser_native.json"));
        no_content["message"].erase("content");
        check(parse_chat_response(no_content.dump(), known).has_value(), "Native missing content");
        auto unknown = parse_tool_content(R"({"tool_name":"other","arguments":{}})", known);
        check(!unknown && unknown.error().code == ParseErrorCode::UnknownTool, "Wrong unknown code");
        auto invalid_args = parse_tool_content(R"({"tool_name":"calculator","arguments":true})");
        check(!invalid_args && invalid_args.error().code == ParseErrorCode::InvalidArguments, "Wrong args code");
        for (const auto& text : {"A normal answer", "", "[{\"subject\":\"OOP\"}]", "{\"classes\":[]}"}) {
            auto answer = parse_chat_response(json{{"message", {{"content", text}, {"tool_calls", json::array()}}}}.dump());
            check(answer && answer->calls.empty() && answer->content == text, "Final answer changed");
        }
        check(counter->executions == 0, "Parser executed a Tool");
        check(!registry.isToolAllowed("task_lookup"), "Parser changed policy");
        std::cout << "PASS " << checks << " parser checks (Release-safe)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
