#include <iostream>
#include <string>
#include <tiny-mcp/mcp_server.h>
#include "notes.h"

int main() {
    std::ios_base::sync_with_stdio(false);

    McpToolServer server("notes-server", "0.1.0");
    NoteStore store;

    // --- Register tools ---

    using AddParams = std::tuple<
        ToolParam<"title", "The title of the note", std::string>,
        ToolParam<"body",  "The body of the note",  std::string>
    >;

    server.tools().register_tool<AddParams>(
        "add_note",
        "Store a note using its title as key",
        [&store](AddParams& params) -> std::string {
            const auto& title = std::get<0>(params);
            const auto& body  = std::get<1>(params);
            store.add(title, body);
            return "Added note '" + std::string(title) + "'";
        }
    );

    using GetParams = std::tuple<
        ToolParam<"title", "The title of the note to retrieve", std::string>
    >;

    server.tools().register_tool<GetParams>(
        "get_note",
        "Retrieve a stored note by title",
        [&store](GetParams& params) -> std::string {
            const auto& title = std::get<0>(params);
            return store.get(title).value_or("No note found with that title.");
        }
    );

    using ListParams = std::tuple<>;

    server.tools().register_tool<ListParams>(
        "list_notes",
        "List all stored note titles",
        [&store](ListParams&) -> std::string {
            auto titles = store.list();
            if (titles.empty()) return "No notes stored.";
            std::string result;
            for (const auto& t : titles)
                result += "- " + t + "\n";
            return result;
        }
    );

    // --- Main loop ---

    std::cerr << "[notes] server started" << std::endl;

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
