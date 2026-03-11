#include <tiny-mcp/mcp_server.h>
#include <iostream>
#include <ostream>

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
// TOOL LISTING — delegates to tool_set, appends backchannel descriptor
// ============================================================================

json McpToolServer::handle_tools_list(const json& id) {
    json tools = tools_.list_tools();

    if (backchannel_enabled_) {
        tools.push_back({
            {"name",        "backchannel_event"},
            {"description", "Long-poll tool for receiving server-initiated events. "
                            "Call this from a background subagent. "
                            "Returns a JSON array of event strings. "
                            "If include_queued is true (default) and events are already "
                            "queued, returns them immediately; otherwise waits until the "
                            "server calls emit_backchannel_event(). "
                            "After receiving events, re-establish the channel by calling "
                            "this tool again from a new background subagent."},
            {"inputSchema", {
                {"type",       "object"},
                {"properties", {
                    {"include_queued", {
                        {"type",        "boolean"},
                        {"description", "Return any queued events immediately if available. "
                                        "Set to false to ignore queued events and wait for "
                                        "the next fresh event only."}
                    }}
                }},
                {"required", json::array()}
            }}
        });
    }

    return json(JsonRpcResponse{id, {{"tools", tools}}});
}

// ============================================================================
// TOOL DISPATCH — delegates to tool_set, intercepts backchannel_event
// ============================================================================

std::optional<json> McpToolServer::handle_tools_call(const json& id, const json& params) {
    std::string name = params.value("name", "");
    json arguments = params.value("arguments", json::object());

    std::cerr << "[mcp] tools/call: " << name << std::endl;

    if (backchannel_enabled_ && name == "backchannel_event") {
        // If a previous subagent is still waiting, unblock it with a superseded
        // sentinel before registering the new caller.
        if (pending_backchannel_id_.has_value()) {
            json events = json::array();
            events.push_back("backchannel_superseded");
            json sup_response = {{"content", {{{"type", "text"}, {"text", events.dump()}}}}};
            *backchannel_out_ << json(JsonRpcResponse{*pending_backchannel_id_, sup_response}) << "\n";
            backchannel_out_->flush();
            pending_backchannel_id_.reset();
        }

        bool include_queued = arguments.value("include_queued", true);
        if (include_queued && !event_queue_.empty()) {
            json events = json::array();
            for (auto& msg : event_queue_) events.push_back(msg);
            event_queue_.clear();
            json response = {{"content", {{{"type", "text"}, {"text", events.dump()}}}}};
            return json(JsonRpcResponse{id, response});
        }
        pending_backchannel_id_ = id;
        return std::nullopt;
    }

    auto result = tools_.call_tool(name, arguments);

    std::string text = result ? *result : result.error();
    json response = {{"content", {{{"type", "text"}, {"text", text}}}}};
    if (!result) {
        response["isError"] = true;
    }
    return json(JsonRpcResponse{id, response});
}

// ============================================================================
// BACKCHANNEL — enable and event emission
// ============================================================================

void McpToolServer::enable_backchannel(std::ostream& out, std::size_t max_queue_size) {
    backchannel_enabled_ = true;
    backchannel_out_     = &out;
    max_queue_size_      = max_queue_size;

    using P = std::tuple<>;
    tools_.register_tool<P>(
        "backchannel_usage",
        "Describes how to use the backchannel notification system.",
        [](P&) -> std::expected<std::string, std::string> {
            return
                "BACKCHANNEL USAGE\n"
                "\n"
                "The backchannel lets this server push events to you without you polling.\n"
                "\n"
                "HOW TO USE:\n"
                "  1. Launch a background subagent whose only job is to call backchannel_event.\n"
                "  2. The call will block until the server has an event to deliver.\n"
                "  3. When the subagent finishes, inspect its result — a JSON array of strings.\n"
                "  4. React to the events in the foreground.\n"
                "  5. Re-establish the channel: launch a new background subagent calling\n"
                "     backchannel_event again.\n"
                "\n"
                "PARAMETERS:\n"
                "  include_queued (bool, default true):\n"
                "    If true and events were emitted while no backchannel was active,\n"
                "    those queued events are returned immediately.\n"
                "    If false, only events emitted after this call are returned.\n"
                "\n"
                "NOTE: A backchannel_event call always returns at least one event.\n"
                "      It never returns an empty array.\n";
        }
    );
}

void McpToolServer::emit_backchannel_event(const std::string& message) {
    if (!backchannel_enabled_) return;

    if (pending_backchannel_id_.has_value()) {
        json events = json::array();
        events.push_back(message);
        json response = {{"content", {{{"type", "text"}, {"text", events.dump()}}}}};
        *backchannel_out_ << json(JsonRpcResponse{*pending_backchannel_id_, response}) << "\n";
        backchannel_out_->flush();
        pending_backchannel_id_.reset();
    } else {
        if (max_queue_size_ > 0 && event_queue_.size() >= max_queue_size_) {
            event_queue_.pop_front();
        }
        event_queue_.push_back(message);
    }
}
