#pragma once

// mcp_server.h — MCP protocol handling.
//
// This file contains all the MCP-specific logic: initialization handshake,
// tool advertisement, tool dispatch, and JSON-RPC formatting.
//
// THE MCP PROTOCOL IN A NUTSHELL:
//
//   MCP (Model Context Protocol) lets AI assistants like Claude use external
//   tools. The communication uses JSON-RPC 2.0 over stdio:
//
//   1. Claude Code spawns our process and talks to it via stdin/stdout
//   2. Initialization handshake:
//      - Client sends "initialize" request (with protocol version, capabilities)
//      - Server responds (with its own protocol version, capabilities, server info)
//      - Client sends "initialized" notification (no response expected)
//   3. Normal operation:
//      - Client calls "tools/list" to discover what tools we offer
//      - Client calls "tools/call" to invoke a specific tool
//   4. Shutdown:
//      - Client closes stdin (EOF), we exit
//
// JSON-RPC 2.0 MESSAGE TYPES:
//
//   Request (expects a response):
//     {"jsonrpc":"2.0", "id":1, "method":"tools/list", "params":{}}
//
//   Response (success):
//     {"jsonrpc":"2.0", "id":1, "result":{...}}
//
//   Response (error):
//     {"jsonrpc":"2.0", "id":1, "error":{"code":-32601, "message":"Method not found"}}
//
//   Notification (no response expected, no "id" field):
//     {"jsonrpc":"2.0", "method":"notifications/initialized"}
//

#include <deque>
#include <functional>
#include <iostream>
#include <optional>
#include <unordered_map>
#include "mcp_types.h"
#include "mcp_tool_set.h"

class McpToolServer {
public:
    McpToolServer(std::string name, std::string version);

    // Process one incoming JSON-RPC message.
    // Returns a JSON response to send back, or nullopt for notifications
    // (which don't get a response per JSON-RPC 2.0 spec) or for a deferred
    // backchannel_event call that is waiting for an event.
    std::optional<json> handle_message(const json& message);

    // Access the tool set for external tool registration.
    tool_set& tools() { return tools_; }

    // Enable the backchannel: registers backchannel_event and backchannel_usage
    // tools.  out is the stream where deferred responses will be written (defaults
    // to std::cout).  max_queue_size caps the ring buffer (0 = unbounded).
    void enable_backchannel(std::ostream& out = std::cout,
                            std::size_t max_queue_size = 0);

    // Deliver an event to the backchannel.
    //   - If a backchannel_event call is pending, writes the JSON-RPC response
    //     to the configured stream immediately and clears the pending call.
    //   - Otherwise enqueues the message (ring-buffer drop-oldest when full).
    // Must not be called before enable_backchannel().
    void emit_backchannel_event(const std::string& message);

private:
    std::string name_;
    std::string version_;
    tool_set tools_;

    // --- Method dispatch table ---
    // Maps JSON-RPC method names to handler functions.
    // Each handler takes (id, params) and returns a JSON-RPC response,
    // or nullopt for responses that will be sent later (deferred).
    using MethodHandler = std::function<std::optional<json>(const json& id, const json& params)>;
    std::unordered_map<std::string, MethodHandler> methods_;

    // --- Registration helpers (called from constructor) ---
    void register_methods();

    // --- MCP protocol handlers ---
    json handle_initialize(const json& id, const json& params);
    json handle_tools_list(const json& id);
    std::optional<json> handle_tools_call(const json& id, const json& params);

    // --- Backchannel state ---
    bool                     backchannel_enabled_    = false;
    std::ostream*            backchannel_out_        = nullptr;
    std::optional<json>      pending_backchannel_id_;
    std::deque<std::string>  event_queue_;
    std::size_t              max_queue_size_         = 0;  // 0 = unbounded
};
