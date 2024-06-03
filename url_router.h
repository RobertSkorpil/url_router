#include <array>
#include <cstddef>
#include <algorithm>
#include <vector>
#include <string>
#include <string_view>
#include <tuple>
#include <charconv>
#include <functional>
#include <boost/url.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast.hpp>
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

	consteval operator std::string_view() const
	{
		return { str.begin(), str.end() };
	}
};

template<bool val, typename T>
struct bool_const
{
	static constexpr bool value{ val };
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

	operator T& ()
	{
		return value;
	}

	T* operator ->()
	{
		return &value;
	}

	T& operator *()
	{
		return value;
	}
};

template<literal l, typename T>
struct query_arg
{
	static constexpr auto name{ l };
	T value;

	operator T& ()
	{
		return value;
	}

	T* operator ->()
	{
		return &value;
	}

	T& operator *()
	{
		return value;
	}
};

struct url_arg
{
	boost::urls::url_view url;

	operator boost::urls::url_view()
	{
		return url;
	}

	boost::urls::url_view *operator ->()
	{
		return &url;
	}

	boost::urls::url_view& operator *()
	{
		return url;
	}
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

struct ignore_t {};

template<typename pattern, typename argument_tuple>
struct arg_finder 
{
	using type = ignore_t;
	using arg = void;
};

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
	using arg = void;
};

template<literal L, literal L2, typename T, typename...Args>
struct arg_finder<argument_pattern<L>, std::tuple<path_arg<L2, T>, Args...>>
{
	using next_arg_finder = arg_finder<argument_pattern<L>, std::tuple<Args...>>;
	using type = next_arg_finder::type;
	using arg = next_arg_finder::arg;
};

template<literal route_string, typename result_t>
struct basic_endpoint
{
	using route = decltype(parse_route_string<route_string>());
	using return_type_t = result_t;

	result_t value;
	basic_endpoint(return_type_t&& value) : value{ std::forward<return_type_t>(value) } {};
};

template<literal route_string>
using endpoint = boost::asio::awaitable<basic_endpoint<route_string, boost::beast::http::response<boost::beast::http::string_body>>>;

template<typename T>
struct route_extractor {};

/*
template<typename endpoint, typename...args_>
struct route_extractor<endpoint(*)(args_...)>
{
	using route = endpoint::route;
	using args = std::tuple<args_...>;
	using return_type = endpoint::return_type_t;
};
*/

template<typename endpoint, typename...args_>
struct route_extractor<boost::asio::awaitable<endpoint>(*)(args_...)>
{
	static constexpr bool is_awaitable{ true };
	using route = endpoint::route;
	using args = std::tuple<args_...>;
	using return_type = boost::asio::awaitable<typename endpoint::return_type_t>;
};

/*
template<typename klass, typename endpoint, typename...args_>
struct route_extractor<endpoint(klass::*)(args_...)>
{
	static constexpr bool is_awaitable{ false };
	using route = endpoint::route;
	using args = std::tuple<klass *, args_...>;
	using return_type = endpoint::return_type_t;
};
*/

template<typename klass, typename endpoint, typename...args_>
struct route_extractor<boost::asio::awaitable<endpoint>(klass::*)(args_...)>
{
	static constexpr bool is_awaitable{ true };
	using route = endpoint::route;
	using args = std::tuple<klass *, args_...>;
	using return_type = boost::asio::awaitable<endpoint>;
};	

template<auto...routes>
struct router_t
{
private:
	template<typename argument_tuple, typename T, typename tuple>
	struct single_pattern_matcher {};

	template<typename argument_tuple, literal L, typename tuple>
	struct single_pattern_matcher<argument_tuple, fixed_pattern<L>, tuple>
	{
		bool operator()(tuple& values, std::string_view::const_iterator& begin, std::string_view::const_iterator end) const
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

	template<typename argument_tuple, typename tuple>
	struct single_pattern_matcher<argument_tuple, argument_pattern<"*">, tuple>
	{
		bool operator()(tuple& values, std::string_view::const_iterator& begin, std::string_view::const_iterator end) const
		{
			begin = end;
			return true;
		}
	};

	template<typename argument_tuple, literal L, typename tuple>
	struct single_pattern_matcher<argument_tuple, argument_pattern<L>, tuple>
	{
		bool operator()(tuple& values, std::string_view::const_iterator& begin, std::string_view::const_iterator end) const
		{
			using argument_finder = arg_finder<argument_pattern<L>, argument_tuple>;
			using type = argument_finder::type;
			using arg = argument_finder::arg;

			if constexpr (std::is_same_v<type, ignore_t>)
			{
				return true;
			}
			else if constexpr (std::is_integral_v<type>)
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
		bool operator()(tuple& values, std::string_view url) const
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
	bool matches(std::string_view url, tuple& values)
	{
		using pattern_tuple = dechain<typename route::route>::tuple;
		using matcher = pattern_matcher<tuple, typename route::args, pattern_tuple>;
		if (matcher{}(values, url))
			return true;
		else
			return false;
	}

	struct route_context
	{
		boost::urls::url url;
		boost::urls::params_ref params{ url.params() };
	};

	template<typename T>
	struct non_path_arg_filler 
	{
		template<typename tuple>
		static void fill(tuple& values, const route_context& ctx) {}
	};

	template<literal L, typename T>
	struct non_path_arg_filler<query_arg<L, T>>
	{
		template<typename tuple>
		static void fill(tuple& values, const route_context& ctx)
		{
			if (auto i{ ctx.params.find(static_cast<std::string_view>(L)) }; i != ctx.params.end())
			{
				auto val{ (*i).value };
				if constexpr (std::is_integral_v<T>)
				{
					if (auto [match, str] { ctre::match<R"(^(\d+).*)">(val.begin(), val.end()) }; match)
					{
						auto result{ std::from_chars(str.data(), str.data() + str.size(), std::get<query_arg<L, T>>(values).value) };
						if (result.ec != std::errc{})
							throw std::runtime_error{ "bad parm" };
					}
					else
						throw std::runtime_error{ "bad parm" };
				}
				else if constexpr (std::is_same_v<T, std::string_view>)
					static_assert(bool_const<false, T>, "query_arg must NOT be a string_view. Use string.");
				else if constexpr (std::is_same_v<T, std::string>)
					std::get<query_arg<L, T>>(values) = std::move(val);
			}
		}
	};

	template<>
	struct non_path_arg_filler<url_arg>
	{
		template<typename tuple>
		static void fill(tuple& values, const route_context& ctx)
		{
			std::get<url_arg>(values).url = boost::urls::url_view{ ctx.url };
		}
	};

	template<typename arg, typename tuple>
	void fill_non_path_arg(tuple& values, const route_context& ctx)
	{
		non_path_arg_filler<arg>::fill(values, ctx);
	}

	template<typename... args>
	void fill_non_path_args(std::tuple<args...>& values, const route_context& ctx)
	{
		((fill_non_path_arg<args>(values, ctx)), ...);
	}

	template<typename first, typename... rest>
	struct first_type_getter
	{
		using type = first;
	};

	using return_type = typename route_extractor<typename first_type_getter<decltype(routes)...>::type>::return_type;
	static constexpr bool is_async{ route_extractor<typename first_type_getter<decltype(routes)...>::type>::is_awaitable };

	template <typename T, typename Tuple>
	struct has_type;

	template <typename T, typename... Us>
	struct has_type<T, std::tuple<Us...>> : std::disjunction<std::is_same<T, Us>...> {};

	template<typename explicit_args_tuple>
	return_type try_route(explicit_args_tuple expl_args, const route_context& ctx)
	{
		throw std::runtime_error{ "no route" };
	}

	template<typename value_tuple, typename tuple>
	struct explicit_args_filler {};

	template<typename value_tuple, typename...explicit_args>
	struct explicit_args_filler<value_tuple, std::tuple<explicit_args...>>
	{
		static void fill(value_tuple& tuple, const std::tuple<explicit_args...>& expl_args)
		{
			(([&] { if constexpr (has_type<explicit_args, value_tuple>::value) std::get<explicit_args>(tuple) = std::get<explicit_args>(expl_args); }()), ...);
		}
	};

	template<typename explicit_args_tuple, auto route, auto...other_routes>
	return_type try_route(explicit_args_tuple expl_args, const route_context& ctx)
	{
		using re = route_extractor<decltype(route)>;
		using tuple = re::args;
		tuple values{};
		explicit_args_filler<tuple, explicit_args_tuple>::fill(values, expl_args);
		ctx.url.path();
		if (matches<re>(ctx.url.path(), values))
		{
			fill_non_path_args(values, ctx);
			if constexpr (re::is_awaitable)
				co_return (co_await std::apply(route, values)).value;
			else
				return std::apply(route, values).value;
		}
		else
		{
			if constexpr (re::is_awaitable)
				co_return co_await try_route<explicit_args_tuple, other_routes...>(expl_args, ctx);
			else
				return try_route<explicit_args_tuple, other_routes...>(expl_args, ctx);
		}
	}
public:
	template<typename...explicit_args>
	return_type route(std::string url, explicit_args...expl_args)
	{
		auto parsed_url{ boost::urls::parse_origin_form(url) };
		if (parsed_url.has_error())
			throw std::runtime_error{ "url parse error" };
		route_context ctx{ *parsed_url };
		if constexpr (is_async)
			co_return co_await try_route<std::tuple<explicit_args...>, routes...>(std::make_tuple(expl_args...), ctx);
		else
			return try_route<std::tuple<explicit_args...>, routes...>(std::make_tuple(expl_args...), ctx);
	}
};
