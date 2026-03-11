#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <tiny-mcp/mcp_server.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

using Ret = std::expected<std::string, std::string>;

static McpToolServer make_server() {
    return McpToolServer("test-server", "1.2.3");
}

static json make_request(int id, const std::string& method, json params = json::object()) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}

static json make_notification(const std::string& method) {
    return {{"jsonrpc", "2.0"}, {"method", method}};
}

// ---------------------------------------------------------------------------
// TEST CASES
// ---------------------------------------------------------------------------

TEST_SUITE("McpToolServer") {

TEST_CASE("initialize response has required fields") {
    auto srv = make_server();
    auto resp = srv.handle_message(make_request(1, "initialize", {
        {"protocolVersion", "2024-11-05"},
        {"capabilities",    json::object()},
        {"clientInfo",      {{"name","test"},{"version","0"}}}
    }));

    REQUIRE(resp.has_value());
    CHECK((*resp)["id"]   == 1);
    CHECK((*resp)["jsonrpc"] == "2.0");
    const json& result = (*resp)["result"];
    CHECK(result["protocolVersion"] == "2024-11-05");
    CHECK(result.contains("capabilities"));
    CHECK(result["serverInfo"]["name"]    == "test-server");
    CHECK(result["serverInfo"]["version"] == "1.2.3");
}

TEST_CASE("notification (no id) returns nullopt") {
    auto srv = make_server();
    auto resp = srv.handle_message(make_notification("notifications/initialized"));
    CHECK_FALSE(resp.has_value());
}

TEST_CASE("tools/list returns registered tool with correct schema") {
    auto srv = make_server();
    using P = std::tuple<ToolParam<"n", "a number", int>>;
    srv.tools().register_tool<P>("square", "squares n",
        [](P& p) -> Ret { int v = std::get<0>(p); return std::to_string(v * v); });

    auto resp = srv.handle_message(make_request(2, "tools/list"));
    REQUIRE(resp.has_value());

    const json& tools = (*resp)["result"]["tools"];
    REQUIRE(tools.is_array());
    REQUIRE(tools.size() == 1);
    CHECK(tools[0]["name"] == "square");
    CHECK(tools[0]["description"] == "squares n");
    CHECK(tools[0]["inputSchema"]["properties"]["n"]["type"] == "integer");
}

TEST_CASE("tools/call success: result has content text and no isError") {
    auto srv = make_server();
    using P = std::tuple<ToolParam<"x", "value", int>>;
    srv.tools().register_tool<P>("double", "doubles x",
        [](P& p) -> Ret { return std::to_string(2 * int(std::get<0>(p))); });

    auto resp = srv.handle_message(make_request(3, "tools/call", {
        {"name",      "double"},
        {"arguments", {{"x", 7}}}
    }));

    REQUIRE(resp.has_value());
    const json& result = (*resp)["result"];
    CHECK(result["content"][0]["text"] == "14");
    CHECK(result["content"][0]["type"] == "text");
    CHECK_FALSE(result.contains("isError"));
}

TEST_CASE("tools/call where tool returns std::unexpected → isError=true") {
    auto srv = make_server();
    using P = std::tuple<>;
    srv.tools().register_tool<P>("boom", "always fails",
        [](P&) -> Ret { return std::unexpected("kaboom"); });

    auto resp = srv.handle_message(make_request(4, "tools/call", {
        {"name",      "boom"},
        {"arguments", json::object()}
    }));

    REQUIRE(resp.has_value());
    const json& result = (*resp)["result"];
    CHECK(result["isError"] == true);
    CHECK(result["content"][0]["text"] == "kaboom");
}

TEST_CASE("tools/call unknown tool → isError=true") {
    auto srv = make_server();
    auto resp = srv.handle_message(make_request(5, "tools/call", {
        {"name",      "no_such_tool"},
        {"arguments", json::object()}
    }));

    REQUIRE(resp.has_value());
    CHECK((*resp)["result"]["isError"] == true);
}

TEST_CASE("tools/call missing required argument → isError=true") {
    auto srv = make_server();
    using P = std::tuple<ToolParam<"q", "required", std::string>>;
    srv.tools().register_tool<P>("needs_q", "needs q",
        [](P& p) -> Ret { return std::string(std::get<0>(p)); });

    auto resp = srv.handle_message(make_request(6, "tools/call", {
        {"name",      "needs_q"},
        {"arguments", json::object()}  // missing "q"
    }));

    REQUIRE(resp.has_value());
    CHECK((*resp)["result"]["isError"] == true);
    CHECK((*resp)["result"]["content"][0]["text"].get<std::string>().find("Invalid arguments")
          != std::string::npos);
}

TEST_CASE("unknown method returns JSON-RPC error response with code -32601") {
    auto srv = make_server();
    auto resp = srv.handle_message(make_request(7, "no/such/method"));

    REQUIRE(resp.has_value());
    CHECK((*resp)["id"] == 7);
    CHECK((*resp)["error"]["code"] == -32601);
}

TEST_CASE("ping method returns empty result") {
    auto srv = make_server();
    auto resp = srv.handle_message(make_request(8, "ping"));
    REQUIRE(resp.has_value());
    CHECK((*resp)["result"] == json::object());
}

} // TEST_SUITE
