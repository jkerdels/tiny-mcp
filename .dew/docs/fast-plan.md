# Fast Implementation Plan: backchannel

## What We're Building

A backchannel notification capability for tiny-mcp servers. It lets an MCP server proactively push string events to the agent by exploiting the subagent long-poll pattern: a background subagent calls a special `backchannel_event` tool that defers its response; when the server calls `emit_backchannel_event()`, it writes the JSON-RPC response directly to stdout and the subagent unblocks. A ring-buffer accumulates events that arrive while no backchannel call is pending, preventing loss across reconnects.

## Success Criteria

- Calling `enable_backchannel()` causes `backchannel_event` and `backchannel_usage` to appear in the `tools/list` response.
- A `backchannel_event` call with an empty queue returns `std::nullopt` from `handle_message()` (deferred — no response written).
- `emit_backchannel_event(msg)` with a pending ID writes a well-formed MCP JSON-RPC response to the configured ostream and clears the pending ID.
- `emit_backchannel_event(msg)` with no pending ID enqueues `msg` into the ring buffer.
- A `backchannel_event` call with `include_queued=true` and a non-empty queue returns a JSON array of all queued strings immediately and clears the queue.
- A `backchannel_event` call with `include_queued=false` (or empty queue) always defers.
- Ring buffer respects `max_queue_size`: when full, oldest entry is overwritten (oldest-drop).
- Without `enable_backchannel()`, a `backchannel_event` call is treated as an unknown tool (isError response).
- All existing tests continue to pass.

## Scope

- **In scope**: `McpToolServer` API additions, internal ring buffer, two auto-registered tools, `MethodHandler` type change, unit tests.
- **Out of scope**: threading, async I/O, multiple simultaneous backchannel subscriptions, per-event routing, example integration in the notes server.

## Key Assumptions

| Assumption | Confidence | Consequence if wrong |
|-----------|-----------|----------------------|
| `handle_message()` is called from a single thread | high | No mutex needed on pending ID or queue; if wrong, data races |
| `emit_backchannel_event()` is also called from the same thread (or at least not concurrently with `handle_message()`) | high | Same as above — application is responsible for thread safety |
| The MCP host tolerates a response arriving for a prior request ID at any point while the connection is open | high | Core mechanism breaks; but this is standard JSON-RPC over stdio |
| Changing `MethodHandler` from `std::function<json(...)>` to `std::function<std::optional<json>(...)>` has no external callers that need updating | high | Would need to audit any downstream users |

## Implementation Design

### Data Structures

```cpp
// New private members in McpToolServer
bool                      backchannel_enabled_  = false;
std::ostream*             backchannel_out_      = nullptr;
std::optional<json>       pending_backchannel_id_;
std::deque<std::string>   event_queue_;
std::size_t               max_queue_size_       = 0;   // 0 = unbounded
```

The `event_queue_` acts as a ring buffer: when `max_queue_size_ > 0` and `event_queue_.size() == max_queue_size_`, `pop_front()` before `push_back()`.

### Module / File Layout

| Path | Responsibility |
|------|---------------|
| `include/tiny-mcp/mcp_server.h` | Declare `enable_backchannel`, `emit_backchannel_event`, new private members; update `MethodHandler` typedef |
| `src/mcp_server.cpp` | Implement backchannel logic in `handle_tools_call` and new methods; register two backchannel tools in `enable_backchannel` |
| `tests/test_mcp_server.cpp` | Add backchannel test cases |

### Key Interfaces

```cpp
// McpToolServer additions (public)

// Register backchannel tools and store the output stream.
// max_queue_size == 0 means unbounded.
void enable_backchannel(std::ostream& out = std::cout,
                        std::size_t max_queue_size = 0);

// Deliver an event. If a backchannel call is pending, sends response immediately.
// Otherwise enqueues (ring-buffer semantics if max_queue_size > 0).
void emit_backchannel_event(const std::string& message);
```

```cpp
// MethodHandler typedef change (internal)
// Before:
using MethodHandler = std::function<json(const json& id, const json& params)>;
// After:
using MethodHandler = std::function<std::optional<json>(const json& id, const json& params)>;
```

### Control Flow

**`enable_backchannel(out, max_queue_size)`:**
1. Set `backchannel_enabled_ = true`, `backchannel_out_ = &out`, `max_queue_size_ = max_queue_size`.
2. Register `backchannel_usage` in `tools_` — no parameters, returns a static usage string.
3. Do NOT register `backchannel_event` in `tools_` — it is intercepted before tool dispatch and must be listed separately in `handle_tools_list`.

**`handle_tools_list` (modified):**
1. Obtain `tools_.list_tools()` as before.
2. If `backchannel_enabled_`, append the `backchannel_event` tool descriptor (name, description, `include_queued: bool` parameter schema) to the list.

**`handle_tools_call` (modified, now returns `std::optional<json>`):**
1. Extract `name` and `arguments` from params.
2. If `backchannel_enabled_` and `name == "backchannel_event"`:
   a. Read `include_queued = arguments.value("include_queued", true)`.
   b. If `include_queued && !event_queue_.empty()`: build JSON array from queue, clear queue, return immediate MCP response.
   c. Else: `pending_backchannel_id_ = id`, return `std::nullopt`.
3. Otherwise: dispatch to `tools_.call_tool(name, arguments)` as before, wrap result in MCP response, return it.

**`emit_backchannel_event(message)`:**
1. If `pending_backchannel_id_` has value:
   a. Build JSON array `[message]`.
   b. Wrap in `JsonRpcResponse{*pending_backchannel_id_, mcp_content(events.dump())}`.
   c. Write response + `"\n"` to `*backchannel_out_`, flush.
   d. `pending_backchannel_id_.reset()`.
2. Else (no pending call):
   a. If `max_queue_size_ > 0 && event_queue_.size() >= max_queue_size_`: `event_queue_.pop_front()`.
   b. `event_queue_.push_back(message)`.

### External Dependencies

| Dependency | Behaviour relied upon | Documented? |
|-----------|----------------------|-------------|
| `nlohmann/json` | `json::array()`, `push_back`, `dump()`, `value()` with default | Yes |
| MCP / JSON-RPC over stdio | Server may send a response to an earlier request ID at any later time while connection is open | Yes (JSON-RPC 2.0 spec) |

## Acceptance Tests

1. **tools/list includes backchannel tools**: After `enable_backchannel()`, `handle_tools_list` response contains both `backchannel_event` and `backchannel_usage`.
2. **Deferred on empty queue**: `handle_message(backchannel_event_call)` returns `std::nullopt` when queue is empty.
3. **emit with pending ID**: After step 2, `emit_backchannel_event("hello")` writes a valid JSON-RPC response containing `["hello"]` to the ostream; subsequent `handle_message` returns normally (no lingering pending ID).
4. **emit with no pending ID**: `emit_backchannel_event("x")` with no pending call enqueues `"x"`; next `backchannel_event(include_queued=true)` call returns `["x"]` immediately.
5. **include_queued=false ignores queue**: Even with items in the queue, `backchannel_event(include_queued=false)` defers (queue untouched).
6. **Queue cleared on immediate return**: After step 4, `pending_backchannel_id_` is unset and queue is empty.
7. **Ring-buffer overflow**: With `max_queue_size=2`, emitting three events without a pending call keeps only the two most recent.
8. **backchannel_usage returns non-empty string**: Tool call returns a non-empty, non-error string.
9. **No enable → unknown tool**: Without `enable_backchannel()`, a `backchannel_event` call returns `isError=true`.
10. **All existing tests pass**: No regression on `test_tool_set.cpp` or existing `test_mcp_server.cpp` cases.

## Open Questions / Risks

- **Queue on immediate drain**: When a `backchannel_event` call returns the queue immediately, it does NOT also set `pending_backchannel_id_`. The subagent must re-call to re-establish the long poll. This is intentional but worth noting in `backchannel_usage`.
- **`emit_backchannel_event` thread safety**: Documented as single-threaded only. If a user calls it from a signal handler or timer thread, they get UB. A future follow-up could add a mutex or an eventfd-based wake mechanism.
- **`backchannel_event` schema**: The `include_queued` parameter is a `bool` with default `true`. Verify that the agent framework handles optional boolean parameters correctly.
