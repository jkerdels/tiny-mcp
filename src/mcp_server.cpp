#include <tiny-mcp/mcp_server.h>
#include <iostream>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

McpToolServer::McpToolServer(std::string name, std::string version)
    : name_(std::move(name)), version_(std::move(version))
{
    register_methods();
}

// ============================================================================
// METHOD REGISTRATION
// ============================================================================

void McpToolServer::register_methods() {
    methods_["initialize"] = [this](const json& id, const json& params) {
        return handle_initialize(id, params);
    };

    methods_["ping"] = [](const json& id, const json& /*params*/) {
        return json(JsonRpcResponse{id, json::object()});
    };

    methods_["tools/list"] = [this](const json& id, const json& /*params*/) {
        return handle_tools_list(id);
    };

    methods_["tools/call"] = [this](const json& id, const json& params) {
        return handle_tools_call(id, params);
    };
}

// ============================================================================
// MESSAGE DISPATCH
// ============================================================================

std::optional<json> McpToolServer::handle_message(const json& message) {
    std::string method = message.value("method", "");
    bool has_id = message.contains("id");

    // Notifications have no "id" — process but don't respond.
    if (!has_id) {
        std::cerr << "[mcp] notification: " << method << std::endl;
        return std::nullopt;
    }

    json id = message["id"];
    json params = message.value("params", json::object());

    // Look up the method in the dispatch table.
    auto it = methods_.find(method);
    if (it == methods_.end()) {
        return json(JsonRpcErrorResponse{id, {-32601, "Method not found: " + method}});
    }

    return it->second(id, params);
}

// ============================================================================
// INITIALIZATION
// ============================================================================

json McpToolServer::handle_initialize(const json& id, const json& params) {
    std::cerr << "[mcp] initialize from: "
              << params.value("clientInfo", json::object()).value("name", "unknown")
              << std::endl;

    return json(JsonRpcResponse{id, {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {{"tools", json::object()}}},
        {"serverInfo", {{"name", name_}, {"version", version_}}}
    }});
}

// ============================================================================
// TOOL LISTING — delegates to tool_set
// ============================================================================

json McpToolServer::handle_tools_list(const json& id) {
    return json(JsonRpcResponse{id, {{"tools", tools_.list_tools()}}});
}

// ============================================================================
// TOOL DISPATCH — delegates to tool_set
// ============================================================================

json McpToolServer::handle_tools_call(const json& id, const json& params) {
    std::string name = params.value("name", "");
    json arguments = params.value("arguments", json::object());

    std::cerr << "[mcp] tools/call: " << name << std::endl;

    auto result = tools_.call_tool(name, arguments);

    std::string text = result ? *result : result.error();
    json response = {{"content", {{{"type", "text"}, {"text", text}}}}};
    if (!result) {
        response["isError"] = true;
    }
    return json(JsonRpcResponse{id, response});
}
