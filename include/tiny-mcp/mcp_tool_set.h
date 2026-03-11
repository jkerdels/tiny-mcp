#ifndef TINY_MCP_TOOL_SET_H
#define TINY_MCP_TOOL_SET_H

#include <algorithm>
#include <string_view>
#include <functional>
#include <tuple>
#include <string>
#include <unordered_map>
#include <expected>

#include <nlohmann/json.hpp>
#include <utility>

using json = nlohmann::json;

template<size_t N>
struct StringLiteral {
    constexpr StringLiteral(const char (&str)[N]) {
        std::copy_n(str, N, value);
    }

    [[nodiscard]] constexpr std::string_view sv() const { return {value,N-1}; }

    char value[N];
};


template<
	StringLiteral PN,
	StringLiteral PD,
	typename      T
>
struct ToolParam {
	static constexpr std::string_view param_name        = PN.sv();
	static constexpr std::string_view param_description = PD.sv();
	static constexpr std::string_view param_type        = []{
        if (std::is_same_v<T,bool>)
            return "boolean";
        else if (std::is_integral_v<T>)
            return "integer";
        else if (std::is_floating_point_v<T>)
            return "number";
        else if (std::is_same_v<T,std::string>)
			return "string";
        else if (std::is_same_v<T,json>)
            return "object";
        throw std::domain_error("unsupported type!");
	}();

	T value{};

	ToolParam() = default;

	ToolParam(const T &other) :
		value(other)
	{}

	operator T() { return value; }
	operator T() const { return value; }
};


template<typename T>
concept tuple_like = requires{std::tuple_size<T>::value;};

class tool_set {

	struct tool_t {
		std::string name;
		std::string description;
		std::function<std::expected<std::string,std::string>(json&)> tool_call;
		std::function<json()> input_schema;

		tool_t(
			std::string n, std::string d,
			std::function<std::expected<std::string,std::string>(json&)> &&f,
			std::function<json()> &&inp_s
		) :
			name(std::move(n)),
			description(std::move(d)),
			tool_call(std::move(f)),
			input_schema(std::move(inp_s))
		{}
	};

	std::unordered_map<std::string,tool_t> tools;

public:

	template<tuple_like P>
	void register_tool(
		const std::string                                          &tool_name,
		const std::string                                          &tool_description,
		std::function<std::expected<std::string,std::string>(P&)>  tool
	) {
		tools.insert_or_assign(tool_name, tool_t {
			tool_name,
			tool_description,
			[tool](json &params) -> std::expected<std::string,std::string> {
				P tool_params;
				try {
					std::apply([&params](auto&... p) {
						((p.value = params.at(std::string(p.param_name)).template get<
							std::decay_t<decltype(p.value)>>()), ...);
					}, tool_params);
				} catch (const json::exception &e) {
					return std::unexpected(std::string(std::string("Invalid arguments: ") + e.what()));
				}
				return tool( tool_params );
			},
			[]() -> json {
				json input_schema;
				json props = json::object();
				json req   = json::array();
				std::apply([&props, &req](const auto&... p) {
					((props[std::string(p.param_name)] = json{
						{"type",        std::string(p.param_type)},
						{"description", std::string(p.param_description)}
					}, req.push_back(std::string(p.param_name))), ...);
				}, P{});
				input_schema = {
					{"type", "object"},
					{"properties", props},
					{"required", req}
				};

				return input_schema;
			}
		});
	}

	// Calls a tool by name. Returns the tool's result or an error string.
	std::expected<std::string,std::string> call_tool(const std::string &name, json &arguments) {
		auto it = tools.find(name);
		if (it == tools.end()) {
			return std::unexpected("Unknown tool: " + name);
		}
		return it->second.tool_call(arguments);
	}

	// Returns the "tools" array for a tools/list response.
	// Each entry: {"name": "...", "description": "...", "inputSchema": {...}}
	json list_tools() const {
		json tool_list = json::array();
		for (const auto& [name, t] : tools) {
			tool_list.push_back({
				{"name",        t.name},
				{"description", t.description},
				{"inputSchema", t.input_schema()}
			});
		}
		return tool_list;
	}

};


#endif
