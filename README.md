# tiny-mcp

A minimal C++ framework for building [MCP](https://modelcontextprotocol.io/) (Model Context Protocol) tool servers. It handles the JSON-RPC 2.0 protocol, tool registration, schema generation, and dispatch — so you can focus on writing your tools.

## Features

- **Type-safe tool registration** — define tool parameters as a `std::tuple` of `ToolParam<>` types; input schemas are generated automatically at compile time.
- **JSON-RPC 2.0** — initialization handshake, `tools/list`, `tools/call`, and error handling are built in.
- **stdio transport** — reads JSON-RPC from stdin, writes responses to stdout. No HTTP, no sockets.
- **Single dependency** — only requires [nlohmann/json](https://github.com/nlohmann/json), fetched automatically via CMake.

## Quick start

### 1. Add tiny-mcp to your project

In your `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(
    tiny_mcp
    GIT_REPOSITORY https://github.com/jkerdels/tiny-mcp.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(tiny_mcp)

target_link_libraries(your-server PRIVATE tiny-mcp::tiny-mcp)
```

### 2. Write a server

```cpp
#include <iostream>
#include <tiny-mcp/mcp_server.h>

int main() {
    McpToolServer server("my-server", "0.1.0");

    // Define a tool with typed parameters
    using GreetParams = std::tuple<
        ToolParam<"name", "Name to greet", std::string>
    >;

    server.tools().register_tool<GreetParams>(
        "greet",
        "Say hello to someone",
        [](GreetParams& p) -> std::expected<std::string,std::string> {
            return "Hello, " + std::string(std::get<0>(p)) + "!";
        }
    );

    // Main loop: read JSON-RPC from stdin, write responses to stdout
    json message;
    try {
        while (std::cin >> message) {
            auto response = server.handle_message(message);
            if (response) {
                std::cout << *response << "\n";
                std::cout.flush();
            }
        }
    } catch (const json::parse_error&) {
        // nlohmann::json throws parse_error on EOF instead of setting eofbit
    }
    return 0;
}
```

### 3. Register with Claude Code

```bash
claude mcp add --scope user --transport stdio my-server -- /path/to/your-server
```

This makes the server available across all your projects. Use `--scope project` to share it with your team via the project's `.mcp.json`. You can verify it's registered by typing `/mcp` inside Claude Code.

## Defining tools

Tools are registered with a parameter tuple and a callback:

```cpp
using Params = std::tuple<
    ToolParam<"query",  "Search query",              std::string>,
    ToolParam<"limit",  "Max results",               int>,
    ToolParam<"exact",  "Require exact match",       bool>
>;

server.tools().register_tool<Params>(
    "search",
    "Search for items",
    [](Params& p) -> std::expected<std::string,std::string> {
        auto& query = std::get<0>(p);  // std::string
        auto& limit = std::get<1>(p);  // int
        auto& exact = std::get<2>(p);  // bool
        // ... your logic here ...
        if (query.value.empty())
            return std::unexpected("query must not be empty");
        return "results";
    }
);
```

Tool callbacks return `std::expected<std::string,std::string>`. Return a plain `std::string` for success (implicit conversion), or `std::unexpected("reason")` to signal a tool error back to the calling agent.

Supported parameter types: `std::string`, `int`, `bool`, `double`.

The input schema (JSON Schema) is generated automatically from the `ToolParam` declarations — no manual JSON needed.

## Example

See [`examples/notes/`](examples/notes/) for a complete working example: a simple note storage server with `add_note`, `get_note`, and `list_notes` tools.

```
cd examples/notes
mkdir build && cd build
cmake .. && cmake --build .
```

## Requirements

- C++23
- CMake 3.14+
