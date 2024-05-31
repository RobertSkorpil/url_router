#include <array>
#include <cstddef>
#include <algorithm>
#include <vector>
#include <print>
#include <string_view>
#include <tuple>
#include <charconv>
#include <functional>
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
struct literal_chain {};

template<literal l, size_t from>
consteval auto find_parms()
{
	if constexpr (constexpr auto b{ find_brackets(l, from) }; std::get<1>(b))
		return
		literal_chain<
		fixed_pattern<l.substr<from, std::get<0>(b) - from>()>,
		literal_chain<
		argument_pattern<l.substr<std::get<0>(b) + 1, std::get<1>(b) - 1>()>,
		decltype(find_parms<l, std::get<0>(b) + std::get<1>(b) + 1>())>>{};
	else if constexpr (from == l.str.size())
		return last{};
	else
		return literal_chain<
		fixed_pattern<l.substr<from, l.str.size() - from>()>,
		last>{};
}

template<literal l>
consteval auto parse_route_string()
{
	return find_parms<l, 0>();
}

template<literal L>
consteval auto operator ""_route()
{
	return parse_route_string<L>();
}

template<literal l, typename T>
struct arg
{
	static constexpr auto name{ l };
	T value;
};

template<typename chain>
struct dechain_t {};

template<>
struct dechain_t<last>
{
	using tuple = std::tuple<>;
};

template<typename pattern>
struct dechain_t<literal_chain<pattern, last>>
{
	using tuple = std::tuple<pattern>;
};

template<typename pattern, typename next>
struct dechain_t<literal_chain<pattern, next>>
{
	using tuple = decltype(
		std::tuple_cat(
			std::declval<typename std::tuple<pattern>>(),
			std::declval<typename dechain_t<next>::tuple>()
		));
};

template<typename pattern, typename call_operator, typename = void>
struct arg_finder {};

template<literal L, typename T, typename endpoint, typename Ret, typename...Args>
struct arg_finder<argument_pattern<L>, Ret(endpoint::*)(arg<L, T>, Args...)>
{
	using type = T;
	using arg = arg<L, T>;
};

template<literal L, typename endpoint, typename Ret>
struct arg_finder<argument_pattern<L>, Ret(endpoint::*)()>
{
	using type = std::false_type;
};

template<literal L, literal L2, typename T, typename endpoint, typename Ret, typename...Args>
struct arg_finder<argument_pattern<L>, Ret(endpoint::*)(arg<L2, T>, Args...), std::enable_if_t<L != L2>>
{
	using next_arg_finder = arg_finder<argument_pattern<L>, Ret(endpoint::*)(Args...)>;
};

template<typename T>
struct parms_to_tupler {};

template<typename endpoint, typename Ret, typename...Args>
struct parms_to_tupler<Ret(endpoint::*)(Args...)>
{
	using type = std::tuple<Args...>;
};

template<typename T>
concept route_like = requires { T::route; };

template<route_like...routes>
struct router_t
{
	template<int = 0>
	auto try_route(std::string_view url)
	{
		return 1;
		throw std::runtime_error{ "no route" };
	}

	template<typename call_operator, typename T, typename tuple>
	struct single_pattern_matcher {};

	template<typename call_operator, literal L, typename tuple>
	struct single_pattern_matcher<call_operator, fixed_pattern<L>, tuple>
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

	template<typename call_operator, literal L, typename tuple>
	struct single_pattern_matcher<call_operator, argument_pattern<L>, tuple>
	{
		bool operator()(tuple &values, std::string_view::const_iterator& begin, std::string_view::const_iterator end) const
		{
			using argument_finder = arg_finder<argument_pattern<L>, call_operator>;
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

	template<typename tuple, typename call_operator, typename T>
	struct pattern_matcher {};

	template<typename tuple, typename call_operator, typename...patterns>
	struct pattern_matcher<tuple, call_operator, std::tuple<patterns...>>
	{
		bool operator()(tuple &values, std::string_view url) const
		{
			auto i{ url.begin() };
			return (single_pattern_matcher<call_operator, patterns, tuple>{}(values, i, url.end()) && ...);
		}
	};

	bool match(std::string_view url)
	{
		return true;
	}

	template<route_like route, typename tuple>
	bool matches(std::string_view url, tuple &values)
	{
		using matcher = pattern_matcher<tuple, decltype(&route::operator ()), typename dechain_t<std::decay_t<decltype(route::route)>>::tuple>;
        std::println("{}", typeid(matcher).name());
		if (matcher{}(values, url))
			return true;
		else
			return false;
	}

	template<route_like route, route_like...other_routes>
	auto try_route(std::string_view url)
	{
        using tuple = parms_to_tupler<decltype(&route::operator ())>::type;
        tuple values{};
		if (matches<route>(url, values))
			return std::apply([](auto...args) { return route{}(args...); }, values);
		else
			return try_route<other_routes...>(url);
	}

	auto route(std::string_view url)
	{
		return try_route<routes...>(url);
	}
};


struct endpoint_1
{
	static constexpr auto route = "/product/<id>/xxx"_route;

	auto operator()(arg<"id", uint32_t> id)
	{
		return 1;
	}
};
router_t<endpoint_1> router{};

int main()
{
	std::println("{}", typeid(router).name());
	router.route("/product/1/xxx");
	return 0;
}
