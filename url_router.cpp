#include <array>
#include <cstddef>
#include <algorithm>
#include <vector>
#include <print>
#include <string_view>
#include <tuple>
#include <charconv>
#include <functional>
#include <boost/url.hpp>
#include <ctre.hpp>

template<std::size_t N>
struct literal
{
	std::array<char, N - 1> str;

	static constexpr size_t size{ N - 1 };

	consteval literal(char const(&s)[N])
	{
		std::copy_n(std::begin(s), N - 1, std::begin(str));
	}

	template<size_t off, size_t n>
	consteval auto substr() const
	{
		literal<n + 1> r;
		std::copy_n(std::begin(str) + off, n, std::begin(r.str));
		return r;
	}

	consteval literal()
	{
		std::fill(std::begin(str), std::end(str), ' ');
	}
};

template<size_t N>
consteval std::tuple<size_t, ptrdiff_t> find_brackets(literal<N> l, size_t from)
{
	ptrdiff_t first{ -1 };
	for (size_t i{ from }; i < l.size; ++i)
		if (l.str[i] == '<')
			first = i;
		else if (l.str[i] == '>' && first >= 0)
			return { first, i - first };
	return { 0, 0 };
}

struct last {};

template<literal l, typename T = void>
struct argument_pattern {};

template<literal l>
struct fixed_pattern {};

template<typename pattern, typename next>
struct pattern_chain {};

template<literal l, size_t from>
consteval auto find_patterns()
{
	if constexpr (constexpr auto b{ find_brackets(l, from) }; std::get<1>(b))
		return
		pattern_chain<
		fixed_pattern<l.substr<from, std::get<0>(b) - from>()>,
		pattern_chain<
		argument_pattern<l.substr<std::get<0>(b) + 1, std::get<1>(b) - 1>()>,
		decltype(find_patterns<l, std::get<0>(b) + std::get<1>(b) + 1>())>>{};
	else if constexpr (from == l.str.size())
		return last{};
	else
		return pattern_chain<
		fixed_pattern<l.substr<from, l.str.size() - from>()>,
		last>{};
}

template<literal l>
consteval auto parse_route_string()
{
	return find_patterns<l, 0>();
}

template<literal l, typename T>
struct path_arg
{
	static constexpr auto name{ l };
	T value;
};

template<literal l, typename T>
struct query_arg
{
	static constexpr auto name{ l };
	T value;
};

template<typename chain>
struct dechain {};

template<>
struct dechain<last>
{
	using tuple = std::tuple<>;
};

template<typename pattern>
struct dechain<pattern_chain<pattern, last>>
{
	using tuple = std::tuple<pattern>;
};

template<typename pattern, typename next>
struct dechain<pattern_chain<pattern, next>>
{
	using tuple = decltype(
		std::tuple_cat(
			std::declval<typename std::tuple<pattern>>(),
			std::declval<typename dechain<next>::tuple>()
		));
};

template<typename pattern, typename argument_tuple, typename = void>
struct arg_finder {};

template<literal L, typename T, typename...Args>
struct arg_finder<argument_pattern<L>, std::tuple<path_arg<L, T>, Args...>>
{
	using type = T;
	using arg = path_arg<L, T>;
};

template<literal L>
struct arg_finder<argument_pattern<L>, void>
{
	using type = std::false_type;
};

template<literal L, literal L2, typename T, typename...Args>
struct arg_finder<argument_pattern<L>, std::tuple<path_arg<L2, T>, Args...>, void>
{
	using next_arg_finder = arg_finder<argument_pattern<L>, std::tuple<Args...>>;
	using type = next_arg_finder::type;
	using arg = next_arg_finder::arg;
};

template<literal route_string>
struct endpoint
{
	using route = decltype(parse_route_string<route_string>());
	using return_type_t = int;

	return_type_t value;
	endpoint(return_type_t&& value) : value{ std::forward<return_type_t>(value) } {};
};

template<typename T>
struct route_extractor {};

template<typename endpoint, typename...args_>
struct route_extractor<endpoint(*)(args_...)>
{
	using route = endpoint::route;
	using args = std::tuple<args_...>;
};

template<auto...routes>
struct router_t
{
private:
	template<int = 0>
	auto try_route(boost::urls::url_view url)
	{
		return 1;
		throw std::runtime_error{ "no route" };
	}

	template<typename argument_tuple, typename T, typename tuple>
	struct single_pattern_matcher {};

	template<typename argument_tuple, literal L, typename tuple>
	struct single_pattern_matcher<argument_tuple, fixed_pattern<L>, tuple>
	{
		bool operator()(tuple &values, std::string_view::const_iterator& begin, std::string_view::const_iterator end) const
		{
			if (std::distance(begin, end) < L.size)
				return false;
			if (std::equal(L.str.begin(), L.str.end(), begin))
			{
				begin += L.str.size();
				return true;
			}
			else
				return false;
		}
	};

	template<typename argument_tuple, literal L, typename tuple>
	struct single_pattern_matcher<argument_tuple, argument_pattern<L>, tuple>
	{
		bool operator()(tuple &values, std::string_view::const_iterator& begin, std::string_view::const_iterator end) const
		{
			using argument_finder = arg_finder<argument_pattern<L>, argument_tuple>;
			using type = argument_finder::type;
			using arg = argument_finder::arg;

			if constexpr (std::is_integral_v<type>)
			{
				if (auto [match, str] { ctre::match<R"(^(\d+).*)">(begin, end) }; match)
				{
					begin += str.size();
					auto result{ std::from_chars(str.data(), str.data() + str.size(), std::get<arg>(values).value) };
					return result.ec == std::errc{};
				}
				else
					return false;
			}
			else if constexpr (std::is_same_v<type, std::string_view>)
			{
				if (auto [match, str] { ctre::match<R"(^([^/]+).*)">(begin, end) }; match)
				{
					begin += str.size();
					std::get<arg>(values) = str;
					return true;
				}
				else
					return false;
			}
		}
	};

	template<typename tuple, typename argument_tuple, typename T>
	struct pattern_matcher {};

	template<typename tuple, typename argument_tuple, typename...patterns>
	struct pattern_matcher<tuple, argument_tuple, std::tuple<patterns...>>
	{
		bool operator()(tuple &values, std::string_view url) const
		{
			auto i{ url.begin() };
			return (single_pattern_matcher<argument_tuple, patterns, tuple>{}(values, i, url.end()) && ...);
		}
	};

	bool match(std::string_view url)
	{
		return true;
	}

	template<typename route, typename tuple>
	bool matches(std::string_view url, tuple &values)
	{
		using pattern_tuple = dechain<typename route::route>::tuple;
		using matcher = pattern_matcher<tuple, typename route::args, pattern_tuple>;
		std::println("{}", typeid(matcher).name());
		if (matcher{}(values, url))
			return true;
		else
			return false;
	}

	template<typename arg, typename tuple>
	void fill_non_path_arg(tuple& values, boost::urls::url_view url)
	{
		
	}

	template<typename... args>
	void fill_non_path_args(std::tuple<args...>& values, boost::urls::url_view url)
	{
		((fill_non_path_arg<args>(values, url)), ...);
	}

	template<auto route, auto...other_routes>
	auto try_route(boost::urls::url_view url)
	{
		using re = route_extractor<decltype(route)>;
		using tuple = re::args;
		tuple values{};
		if (matches<re>(url.path(), values))
		{
			fill_non_path_args(values, url);
			return std::apply(route, values).value;
		}
		else
			return try_route<other_routes...>(url);
	}
public:
	auto route(std::string_view url)
	{
		auto parsed_url{ boost::urls::parse_origin_form(url) };
		return try_route<routes...>(parsed_url->path());
	}
};


endpoint<"/product/<category>/<id>">
product(path_arg<"id", uint32_t> id, path_arg<"category", uint32_t> cat)
{
	return 1;
}

endpoint<"/category/<category>">
category(path_arg<"category", uint32_t> cat, query_arg<"count", uint32_t> count)
{
	return 1;
}

router_t<product, category> router{};

int main()
{
	router.route("/product/1/23");
	router.route("/category/234");
	return 0;
}
