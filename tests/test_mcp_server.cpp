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

// ---------------------------------------------------------------------------
// Backchannel helpers
// ---------------------------------------------------------------------------

static json make_backchannel_call(int id, bool include_queued = true) {
    return make_request(id, "tools/call", {
        {"name",      "backchannel_event"},
        {"arguments", {{"include_queued", include_queued}}}
    });
}

// ---------------------------------------------------------------------------
// Backchannel tests
// ---------------------------------------------------------------------------

TEST_SUITE("McpToolServer.backchannel") {

TEST_CASE("tools/list includes both backchannel tools after enable_backchannel") {
    std::ostringstream out;
    auto srv = make_server();
    srv.enable_backchannel(out);

    auto resp = srv.handle_message(make_request(10, "tools/list"));
    REQUIRE(resp.has_value());

    const json& tools = (*resp)["result"]["tools"];
    REQUIRE(tools.is_array());

    auto has_tool = [&](const std::string& name) {
        for (const auto& t : tools)
            if (t["name"] == name) return true;
        return false;
    };
    CHECK(has_tool("backchannel_event"));
    CHECK(has_tool("backchannel_usage"));
}

TEST_CASE("backchannel_event defers when queue is empty") {
    std::ostringstream out;
    auto srv = make_server();
    srv.enable_backchannel(out);

    auto resp = srv.handle_message(make_backchannel_call(20));
    CHECK_FALSE(resp.has_value());
    CHECK(out.str().empty());  // nothing written yet
}

TEST_CASE("emit_backchannel_event with pending ID writes response and clears pending") {
    std::ostringstream out;
    auto srv = make_server();
    srv.enable_backchannel(out);

    srv.handle_message(make_backchannel_call(21));  // deferred
    srv.emit_backchannel_event("hello");

    REQUIRE_FALSE(out.str().empty());
    json written = json::parse(out.str());
    CHECK(written["id"] == 21);
    CHECK(written["jsonrpc"] == "2.0");
    const std::string text = written["result"]["content"][0]["text"];
    json events = json::parse(text);
    REQUIRE(events.is_array());
    REQUIRE(events.size() == 1);
    CHECK(events[0] == "hello");

    // Pending ID must be cleared — another emit goes to the queue.
    out.str("");
    srv.emit_backchannel_event("queued");
    CHECK(out.str().empty());
}

TEST_CASE("emit_backchannel_event without pending ID enqueues; next call returns immediately") {
    std::ostringstream out;
    auto srv = make_server();
    srv.enable_backchannel(out);

    srv.emit_backchannel_event("x");

    auto resp = srv.handle_message(make_backchannel_call(22));
    REQUIRE(resp.has_value());  // returned immediately
    const std::string text = (*resp)["result"]["content"][0]["text"];
    json events = json::parse(text);
    REQUIRE(events.is_array());
    CHECK(events[0] == "x");
    CHECK(out.str().empty());  // nothing written to ostream
}

TEST_CASE("include_queued=false defers even when queue is non-empty") {
    std::ostringstream out;
    auto srv = make_server();
    srv.enable_backchannel(out);

    srv.emit_backchannel_event("ignored");

    auto resp = srv.handle_message(make_backchannel_call(23, false));
    CHECK_FALSE(resp.has_value());  // deferred despite non-empty queue
}

TEST_CASE("immediate return clears the queue") {
    std::ostringstream out;
    auto srv = make_server();
    srv.enable_backchannel(out);

    srv.emit_backchannel_event("a");
    srv.emit_backchannel_event("b");

    auto resp = srv.handle_message(make_backchannel_call(24));
    REQUIRE(resp.has_value());
    const std::string text = (*resp)["result"]["content"][0]["text"];
    json events = json::parse(text);
    CHECK(events.size() == 2);

    // Queue is now empty — next call defers.
    auto resp2 = srv.handle_message(make_backchannel_call(25));
    CHECK_FALSE(resp2.has_value());
}

TEST_CASE("ring-buffer overflow: oldest event is dropped") {
    std::ostringstream out;
    auto srv = make_server();
    srv.enable_backchannel(out, /*max_queue_size=*/2);

    srv.emit_backchannel_event("first");
    srv.emit_backchannel_event("second");
    srv.emit_backchannel_event("third");  // should drop "first"

    auto resp = srv.handle_message(make_backchannel_call(26));
    REQUIRE(resp.has_value());
    const std::string text = (*resp)["result"]["content"][0]["text"];
    json events = json::parse(text);
    REQUIRE(events.size() == 2);
    CHECK(events[0] == "second");
    CHECK(events[1] == "third");
}

TEST_CASE("backchannel_usage returns non-empty string") {
    std::ostringstream out;
    auto srv = make_server();
    srv.enable_backchannel(out);

    auto resp = srv.handle_message(make_request(27, "tools/call", {
        {"name",      "backchannel_usage"},
        {"arguments", json::object()}
    }));

    REQUIRE(resp.has_value());
    const json& result = (*resp)["result"];
    CHECK_FALSE(result.contains("isError"));
    const std::string text = result["content"][0]["text"];
    CHECK_FALSE(text.empty());
}

TEST_CASE("backchannel_event without enable_backchannel returns isError") {
    auto srv = make_server();
    auto resp = srv.handle_message(make_backchannel_call(28));
    REQUIRE(resp.has_value());
    CHECK((*resp)["result"]["isError"] == true);
}

TEST_CASE("emit_backchannel_event before enable_backchannel does nothing") {
    auto srv = make_server();
    // No enable_backchannel() call — must not crash or do anything.
    srv.emit_backchannel_event("should be ignored");
}

TEST_CASE("second backchannel_event supersedes first pending call") {
    std::ostringstream out;
    auto srv = make_server();
    srv.enable_backchannel(out);

    // First call defers.
    auto resp1 = srv.handle_message(make_backchannel_call(30));
    CHECK_FALSE(resp1.has_value());

    // Second call: first should receive "backchannel_superseded", second defers.
    auto resp2 = srv.handle_message(make_backchannel_call(31));
    CHECK_FALSE(resp2.has_value());

    // The superseded response for id=30 must have been written to the stream.
    REQUIRE_FALSE(out.str().empty());
    json written = json::parse(out.str());
    CHECK(written["id"] == 30);
    json events = json::parse(written["result"]["content"][0]["text"].get<std::string>());
    REQUIRE(events.size() == 1);
    CHECK(events[0] == "backchannel_superseded");

    // The new pending call (id=31) should be fulfilled by emit.
    out.str("");
    srv.emit_backchannel_event("real event");
    REQUIRE_FALSE(out.str().empty());
    json written2 = json::parse(out.str());
    CHECK(written2["id"] == 31);
}

} // TEST_SUITE(McpToolServer.backchannel)
