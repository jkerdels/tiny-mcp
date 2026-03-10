#pragma once

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ============================================================================
// JSON-RPC ENVELOPE
// ============================================================================

struct JsonRpcResponse {
    json id;
    json result;
};

inline void to_json(json& j, const JsonRpcResponse& r) {
    j = {{"jsonrpc", "2.0"}, {"id", r.id}, {"result", r.result}};
}

struct JsonRpcErrorDetail {
    int code;
    std::string message;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(JsonRpcErrorDetail, code, message)

struct JsonRpcErrorResponse {
    json id;
    JsonRpcErrorDetail error;
};

inline void to_json(json& j, const JsonRpcErrorResponse& r) {
    j = {{"jsonrpc", "2.0"}, {"id", r.id}, {"error", r.error}};
}
