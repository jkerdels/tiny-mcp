#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <tiny-mcp/mcp_tool_set.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

using Ret = std::expected<std::string, std::string>;

TEST_SUITE("tool_set") {

// ---------------------------------------------------------------------------
// call_tool — success path
// ---------------------------------------------------------------------------

TEST_CASE("call_tool returns value on success") {
    tool_set ts;
    using P = std::tuple<ToolParam<"msg", "a message", std::string>>;
    ts.register_tool<P>("echo", "echoes msg",
        [](P& p) -> Ret { return std::string(std::get<0>(p)); });

    json args = {{"msg", "hello"}};
    auto r = ts.call_tool("echo", args);
    REQUIRE(r.has_value());
    CHECK(r.value() == "hello");
}

TEST_CASE("call_tool plain string return implicitly converts") {
    tool_set ts;
    using P = std::tuple<>;
    ts.register_tool<P>("ping", "ping", [](P&) -> Ret { return "pong"; });

    json args = json::object();
    auto r = ts.call_tool("ping", args);
    REQUIRE(r.has_value());
    CHECK(r.value() == "pong");
}

// ---------------------------------------------------------------------------
// call_tool — error paths
// ---------------------------------------------------------------------------

TEST_CASE("call_tool forwards std::unexpected from tool") {
    tool_set ts;
    using P = std::tuple<>;
    ts.register_tool<P>("fail", "always fails",
        [](P&) -> Ret { return std::unexpected("something went wrong"); });

    json args = json::object();
    auto r = ts.call_tool("fail", args);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == "something went wrong");
}

TEST_CASE("call_tool returns unexpected for missing argument") {
    tool_set ts;
    using P = std::tuple<ToolParam<"x", "required", std::string>>;
    ts.register_tool<P>("needs_x", "needs x", [](P& p) -> Ret { return std::string(std::get<0>(p)); });

    json args = json::object();  // missing "x"
    auto r = ts.call_tool("needs_x", args);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().find("Invalid arguments") != std::string::npos);
}

TEST_CASE("call_tool returns unexpected for unknown tool") {
    tool_set ts;
    json args = json::object();
    auto r = ts.call_tool("no_such_tool", args);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().find("Unknown tool") != std::string::npos);
}

// ---------------------------------------------------------------------------
// list_tools — schema generation
// ---------------------------------------------------------------------------

TEST_CASE("list_tools returns array with correct schema") {
    tool_set ts;
    using P = std::tuple<
        ToolParam<"name",  "user name",  std::string>,
        ToolParam<"count", "how many",   int>,
        ToolParam<"flag",  "a flag",     bool>,
        ToolParam<"ratio", "a ratio",    double>
    >;
    ts.register_tool<P>("multi", "multi-param tool", [](P&) -> Ret { return "ok"; });

    json list = ts.list_tools();
    REQUIRE(list.is_array());
    REQUIRE(list.size() == 1);

    const json& t = list[0];
    CHECK(t["name"] == "multi");
    CHECK(t["description"] == "multi-param tool");

    const json& schema = t["inputSchema"];
    CHECK(schema["type"] == "object");
    CHECK(schema["properties"]["name"]["type"]  == "string");
    CHECK(schema["properties"]["count"]["type"] == "integer");
    CHECK(schema["properties"]["flag"]["type"]  == "boolean");
    CHECK(schema["properties"]["ratio"]["type"] == "number");
    CHECK(schema["properties"]["name"]["description"]  == "user name");
    CHECK(schema["properties"]["count"]["description"] == "how many");

    const json& req = schema["required"];
    CHECK(req.size() == 4);
}

TEST_CASE("list_tools empty tuple produces empty required array") {
    tool_set ts;
    using P = std::tuple<>;
    ts.register_tool<P>("no_params", "no params", [](P&) -> Ret { return "ok"; });

    json list = ts.list_tools();
    REQUIRE(list.size() == 1);
    CHECK(list[0]["inputSchema"]["required"].empty());
    CHECK(list[0]["inputSchema"]["properties"].empty());
}

// ---------------------------------------------------------------------------
// ToolParam json type maps to "object"
// ---------------------------------------------------------------------------

TEST_CASE("ToolParam<json> maps to object type in schema") {
    tool_set ts;
    using P = std::tuple<ToolParam<"data", "json payload", json>>;
    ts.register_tool<P>("takes_json", "takes json",
        [](P& p) -> Ret {
            return std::get<0>(p).value.dump();
        });

    json list = ts.list_tools();
    CHECK(list[0]["inputSchema"]["properties"]["data"]["type"] == "object");

    json args = {{"data", {{"key", "value"}}}};
    auto r = ts.call_tool("takes_json", args);
    REQUIRE(r.has_value());
    CHECK(r.value().find("key") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Multiple tools registered
// ---------------------------------------------------------------------------

TEST_CASE("multiple tools can be registered and all appear in list_tools") {
    tool_set ts;
    using P = std::tuple<>;
    ts.register_tool<P>("a", "tool a", [](P&) -> Ret { return "a"; });
    ts.register_tool<P>("b", "tool b", [](P&) -> Ret { return "b"; });
    ts.register_tool<P>("c", "tool c", [](P&) -> Ret { return "c"; });

    CHECK(ts.list_tools().size() == 3);

    json args = json::object();
    CHECK(ts.call_tool("a", args).value() == "a");
    CHECK(ts.call_tool("b", args).value() == "b");
    CHECK(ts.call_tool("c", args).value() == "c");
}

TEST_CASE("register_tool with same name replaces previous registration") {
    tool_set ts;
    using P = std::tuple<>;
    ts.register_tool<P>("f", "v1", [](P&) -> Ret { return "v1"; });
    ts.register_tool<P>("f", "v2", [](P&) -> Ret { return "v2"; });

    CHECK(ts.list_tools().size() == 1);
    json args = json::object();
    CHECK(ts.call_tool("f", args).value() == "v2");
}

} // TEST_SUITE
