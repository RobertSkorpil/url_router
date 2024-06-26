#pragma once

struct verb_mask
{
	uint64_t value{};

	constexpr verb_mask() {}

	constexpr verb_mask(uint64_t value) : value{ value } {}

	constexpr verb_mask operator |(boost::beast::http::verb verb) const
	{
		return { value | (1 << static_cast<int>(verb))};
	}

	constexpr verb_mask operator |(const verb_mask &b) const
	{
		return { value | b.value };
	}

	constexpr bool operator &(boost::beast::http::verb verb) const
	{
		return { static_cast<bool>(value & (1 << static_cast<int>(verb))) };
	}
};

struct verbs
{
	static constexpr verb_mask get{ verb_mask{} | boost::beast::http::verb::get };
	static constexpr verb_mask post{ verb_mask{} | boost::beast::http::verb::post };
	static constexpr verb_mask put{ verb_mask{} | boost::beast::http::verb::put };
	static constexpr verb_mask delete_{ verb_mask{} | boost::beast::http::verb::delete_ };
	static constexpr verb_mask any{ ~0ull };
};

template<typename return_type>
struct basic_reroute_t : std::function<return_type(std::string)>
{
	using std::function<return_type(std::string)>::function;
};

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

	template<size_t N2>
	consteval literal<N + N2 - 1> operator +(literal<N2> b) const
	{
		literal<N + N2 - 1> r;
		auto out{ std::copy_n(std::begin(str), N - 1, std::begin(r.str)) };
		std::copy_n(std::begin(b.str), N2 - 1, out);
		return r;
	}
};

template<bool val, typename T>
struct bool_const
{
	static constexpr bool value{ val };
};

struct special
{
	enum class type_t { nothing, argument_pattern, asterisk, double_asterisk };
	type_t type;
	std::size_t position;
	std::size_t length;
};

template<size_t N>
consteval special find_special(literal<N> l, size_t from)
{
	bool bracket_found{};
	std::size_t first{};
	for (size_t i{ from }; i < l.size; ++i)
		if (!bracket_found && l.str[i] == '*')
			if(i + 1 == l.size || l.str[i + 1] != '*')
				return { special::type_t::asterisk, i, 1 };
			else 
				return { special::type_t::double_asterisk, i, 2 };
		else if (l.str[i] == '<')
		{
			bracket_found = true;
			first = i;
		}
		else if (l.str[i] == '>' && bracket_found)
			return { special::type_t::argument_pattern, first, i - first };
	return { special::type_t::nothing };
}

struct last {};

template<literal l, typename T = void>
struct argument_pattern {};

template<literal l>
struct fixed_pattern 
{
	static constexpr literal lit{ l };
};

template<typename pattern, typename next>
struct pattern_chain {};

template<literal l, size_t from>
consteval auto find_patterns()
{
	if constexpr (constexpr auto b{ find_special(l, from) }; b.type != special::type_t::nothing)
	{
		if constexpr (b.type == special::type_t::asterisk)
			return pattern_chain<
			fixed_pattern<l.substr<from, b.position - from>()>,
			pattern_chain<
			fixed_pattern<"*">,
			decltype(find_patterns<l, b.position + b.length>())>>{};
		else if constexpr (b.type == special::type_t::double_asterisk)
			return pattern_chain<
			fixed_pattern<l.substr<from, b.position - from>()>,
			pattern_chain<
			fixed_pattern<"**">,
			decltype(find_patterns<l, b.position + b.length>())>>{};
		else if constexpr(b.type == special::type_t::argument_pattern)
			return pattern_chain<
			fixed_pattern<l.substr<from, b.position - from>()>,
			pattern_chain<
			argument_pattern<l.substr<b.position + 1, b.length - 1>()>,
			decltype(find_patterns<l, b.position + b.length + 1>())>>{};
	}
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

template<std::size_t index_, typename pattern, typename argument_tuple>
struct arg_finder 
{
	static constexpr auto index{ index_ };
	using type = ignore_t;
	using arg = void;
};

template<std::size_t index_, literal L, typename T, typename...Args>
struct arg_finder<index_, argument_pattern<L>, std::tuple<path_arg<L, T>, Args...>>
{
	static constexpr auto index{ index_ };
	using type = T;
	using arg = path_arg<L, T>;
};

template<std::size_t index_, literal L>
struct arg_finder<index_, argument_pattern<L>, void>
{
	static constexpr auto index{ index_ };
	using type = std::false_type;
	using arg = void;
};

template<std::size_t index_, literal L, literal L2, typename T, typename...Args>
struct arg_finder<index_, argument_pattern<L>, std::tuple<path_arg<L2, T>, Args...>>
{
	static constexpr auto index{ index_ };
	using next_arg_finder = arg_finder<index + 1, argument_pattern<L>, std::tuple<Args...>>;
	using type = next_arg_finder::type;
	using arg = next_arg_finder::arg;
};

template<verb_mask verb_mask_, literal route_string, typename result_t>
struct basic_endpoint
{
	using route = decltype(parse_route_string<route_string>());

	using return_type_t = result_t;
	static constexpr verb_mask mask{ verb_mask_ };

	result_t value;
	basic_endpoint(return_type_t&& value) : value{ std::forward<return_type_t>(value) } {};
};

template<verb_mask verbs, literal route_string>
using endpoint = boost::asio::awaitable<basic_endpoint<verbs, route_string, boost::beast::http::response<boost::beast::http::string_body>>>;

template<literal route_string>
using get_endpoint = endpoint<verbs::get, route_string>;

template<literal route_string>
using post_endpoint = endpoint<verbs::post, route_string>;

template<literal route_string>
using put_endpoint = endpoint<verbs::put, route_string>;

template<literal route_string>
using delete_endpoint = endpoint<verbs::delete_, route_string>;

template<literal route_string>
using any_endpoint = endpoint<verbs::any, route_string>;

template<typename T>
struct route_extractor {};

template<typename endpoint, typename...args_>
struct route_extractor<endpoint(*)(args_...)>
{
	static constexpr bool is_awaitable{ false };
	static constexpr auto mask{ endpoint::mask };
	using route = endpoint::route;
	using args = std::tuple<args_...>;
	using return_type = endpoint::return_type_t;
};

template<typename endpoint, typename...args_>
struct route_extractor<boost::asio::awaitable<endpoint>(*)(args_...)>
{
	static constexpr bool is_awaitable{ true };
	static constexpr auto mask{ endpoint::mask };
	using route = endpoint::route;
	using args = std::tuple<args_...>;
	using return_type = boost::asio::awaitable<typename endpoint::return_type_t>;
};

template<typename klass, typename endpoint, typename...args_>
struct route_extractor<endpoint(klass::*)(args_...)>
{
	static constexpr bool is_awaitable{ false };
	static constexpr auto mask{ endpoint::mask };
	using route = endpoint::route;
	using args = std::tuple<klass *, args_...>;
	using return_type = endpoint::return_type_t;
};

template<typename klass, typename endpoint, typename...args_>
struct route_extractor<boost::asio::awaitable<endpoint>(klass::*)(args_...)>
{
	static constexpr bool is_awaitable{ true };
	static constexpr auto mask{ endpoint::mask };
	using route = endpoint::route;
	using args = std::tuple<klass *, args_...>;
	using return_type = boost::asio::awaitable<endpoint>;
};	

template<typename T>
concept Router = T::is_router;

template<auto...routes>
struct router_t
{
	static constexpr bool is_router{ true };
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
	struct single_pattern_matcher<argument_tuple, fixed_pattern<"*">, tuple>
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
			using argument_finder = arg_finder<0, argument_pattern<L>, argument_tuple>;
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
		request req;
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
		if (re::mask & ctx.req.method() && matches<re>(ctx.url.path(), values))
		{
			fill_non_path_args(values, ctx);
			if constexpr (re::is_awaitable)
				co_return (co_await std::apply(route, std::move(values))).value;
			else
				return std::apply(route, std::move(values)).value;
		}
		else
		{
			if constexpr (re::is_awaitable)
				co_return co_await try_route<explicit_args_tuple, other_routes...>(std::move(expl_args), ctx);
			else
				return try_route<explicit_args_tuple, other_routes...>(std::move(expl_args), ctx);
		}
	}

	template<typename...explicit_args>
	return_type route_explicit(std::string url, request req, explicit_args...expl_args)
	{
		auto parsed_url{ boost::urls::parse_origin_form(url) };
		if (parsed_url.has_error())
			throw std::runtime_error{ "url parse error" };
		route_context ctx{ *parsed_url, std::move(req) };

		using specific_reroute_t = basic_reroute_t<return_type>;
		if constexpr ((std::is_same_v<specific_reroute_t, explicit_args> || ...))
		{
			using explicit_arg_tuple = std::tuple<request*, explicit_args...>;

			if constexpr (is_async)
				co_return co_await try_route<explicit_arg_tuple, routes...>(std::make_tuple(&ctx.req, expl_args...), ctx);
			else
				return try_route<explicit_arg_tuple, routes...>(std::make_tuple(&ctx.req, expl_args...), ctx);
		}
		else
		{
			specific_reroute_t reroute{
				[&](std::string reroute_url) mutable -> return_type
				{
					if constexpr (is_async)
						co_return co_await route_explicit(std::move(reroute_url), std::move(ctx.req), std::forward<explicit_args>(expl_args)...);
					else
						return route_explicit(std::move(reroute_url), std::move(ctx.req), std::forward<explicit_args>(expl_args)...);
				}
			};

			using explicit_arg_tuple = std::tuple<request*, specific_reroute_t, explicit_args...>;

			if constexpr (is_async)
				co_return co_await try_route<explicit_arg_tuple, routes...>(std::make_tuple(&ctx.req, std::move(reroute), std::forward<explicit_args>(expl_args)...), ctx);
			else
				return try_route<explicit_arg_tuple, routes...>(std::make_tuple(&ctx.req, std::move(reroute), std::forward<explicit_args>(expl_args)...), ctx);
		}
	}
public:
	template<typename...explicit_args>
	return_type route(request req, explicit_args...expl_args)
	{
		std::string target{ req.target() };
		if constexpr (is_async)
			co_return co_await route_explicit<explicit_args...>(std::move(target), std::move(req), std::forward<explicit_args>(expl_args)...);
		else
			return route_explicit<explicit_args...>(std::move(target), std::move(req), std::forward<explicit_args>(expl_args)...);
	}
};

namespace v2
{
	namespace detail
	{
		template<typename T>
		struct filter_out_argument_patterns;

		template<>
		struct filter_out_argument_patterns<std::tuple<>>
		{
			using type = std::tuple<>;
		};

		template<typename first_pattern, typename...other_patterns>
		struct filter_out_argument_patterns<std::tuple<first_pattern, other_patterns...>>
		{
			using type = typename filter_out_argument_patterns<std::tuple<other_patterns...>>::type;
		};

		template<literal L, typename...other_patterns>
		struct filter_out_argument_patterns<std::tuple<argument_pattern<L>, other_patterns...>>
		{
			using type = decltype(std::tuple_cat(std::declval<std::tuple<argument_pattern<L>>>(), std::declval<typename filter_out_argument_patterns<std::tuple<other_patterns...>>::type>()));
		};

		template<typename T>
		struct pattern_classifier;

		template<>
		struct pattern_classifier<fixed_pattern<"*">>
		{
			static constexpr bool is_fixed{ true };
			static constexpr bool is_wildcard{ true };
			static constexpr bool is_asterisk{ true };
			static constexpr bool is_double_asterisk{ false };
			static constexpr bool is_argument{ false };
		};

		template<>
		struct pattern_classifier<fixed_pattern<"**">>
		{
			static constexpr bool is_fixed{ true };
			static constexpr bool is_wildcard{ true };
			static constexpr bool is_asterisk{ false };
			static constexpr bool is_double_asterisk{ true };
			static constexpr bool is_argument{ false };
		};

		template<literal L>
		struct pattern_classifier<fixed_pattern<L>>
		{
			static constexpr bool is_fixed{ true };
			static constexpr bool is_wildcard{ false };
			static constexpr bool is_argument{ false };
		};

		template<literal L>
		struct pattern_classifier<argument_pattern<L>>
		{
			static constexpr bool is_fixed{ false };
			static constexpr bool is_argument{ true };
		};

		template<typename tuple>
		struct tuple_decapitator;

		template<typename first, typename...rest>
		struct tuple_decapitator<std::tuple<first, rest...>>
		{
			using type = std::tuple<rest...>;
		};

		template<typename pattern_tuple, typename function_argument_tuple>
		consteval auto compose_regex()
		{
			if constexpr (std::is_same_v<pattern_tuple, std::tuple<>>)
				return literal{ "" };
			else
			{
				using first_pattern = typename std::decay_t<decltype(std::get<0>(std::declval<pattern_tuple>()))>;
				using classifier = pattern_classifier<first_pattern>;

				auto first_regex{ []() constexpr {
					if constexpr (classifier::is_fixed)
					{
						if constexpr (classifier::is_wildcard)
						{
							if constexpr (classifier::is_asterisk)
								return literal{ R"([^/]*)" };
							else if constexpr (classifier::is_double_asterisk)
								return literal{ R"(.*)" };
						}
						else
							return first_pattern::lit;
					}
					else if constexpr (classifier::is_argument)
					{
						using arg_type = typename arg_finder<0, first_pattern, function_argument_tuple>::type;
						if constexpr (std::is_same_v<arg_type, std::string> || std::is_same_v<arg_type, std::string_view>)
							return literal{ R"(([^/]*))" };
						else if constexpr (std::is_integral_v<arg_type>)
							return literal{ R"((\d+))" };
					}
				}() };

				using other_patterns = tuple_decapitator<pattern_tuple>::type;
				return first_regex + compose_regex<other_patterns, function_argument_tuple>();
			}
		}

		template<typename T>
		struct endpoint_extractor;

		template<typename endpoint, typename...args_>
		struct endpoint_extractor<boost::asio::awaitable<endpoint>(*)(args_...)>
		{
			using type = endpoint;
			using args = std::tuple<args_...>;
		};

		template<typename klass, typename endpoint, typename...args_>
		struct endpoint_extractor<boost::asio::awaitable<endpoint>(klass::*)(args_...)>
		{
			using type = endpoint;
			using args = std::tuple<klass*, args_...>;
		};

		template<size_t N>
		consteval auto literal_to_ctre(literal<N> l)
		{
			return ctll::fixed_string<N - 1>{ ctll::construct_from_pointer, std::data(l.str) };
		}

		template<typename function_argument_tuple, typename...argument_patterns>
		consteval auto create_capture_group_map_impl(std::tuple<argument_patterns...>)
		{
            std::array<size_t, sizeof...(argument_patterns)> result;

            size_t i{};
            ((result[i++] = arg_finder<0, argument_patterns, function_argument_tuple>::index), ...);

            return result;

		}

		template<typename argument_pattern_tuple, typename function_argument_tuple>
		consteval auto create_capture_group_map()
		{
			return create_capture_group_map_impl<function_argument_tuple>(argument_pattern_tuple{});
		}
	}

	template<verb_mask verb_mask_, literal route_string, typename result_t>
	struct basic_endpoint
	{
		static constexpr verb_mask mask{ verb_mask_ };
		using pattern_chain = decltype(parse_route_string<route_string>());
		using pattern_tuple = typename dechain<pattern_chain>::tuple;
		using argument_pattern_tuple = typename detail::filter_out_argument_patterns<pattern_tuple>::type;

		using return_type_t = result_t;

		result_t value;
		basic_endpoint(return_type_t&& value) : value{ std::forward<return_type_t>(value) } {};
	};

	template<verb_mask verb_mask_, literal route_string, typename result_t>
	using async_endpoint = boost::asio::awaitable<basic_endpoint<verb_mask_, route_string, result_t>>;

	namespace detail
	{
        struct route_context
        {
            boost::urls::url url;
            request req;
            boost::urls::params_ref params{ url.params() };
        };

		template<auto endpoint>
		struct route_descriptor
		{
			using endpoint_extractor = typename endpoint_extractor<std::decay_t<decltype(endpoint)>>;
			using endpoint_type = endpoint_extractor::type;
			using function_argument_tuple = endpoint_extractor::args;
			static constexpr literal path_regex{ compose_regex<endpoint_type::pattern_tuple, function_argument_tuple>() };
			static constexpr auto ctre_string{ literal_to_ctre(path_regex) };
			using regex_match_result_type = decltype(ctre::match<ctre_string>(""));
			static constexpr auto capture_group_count{ regex_match_result_type::count() - 1 };
			static constexpr auto capture_group_map{ create_capture_group_map<endpoint_type::argument_pattern_tuple, function_argument_tuple>() };

			template<size_t...i>
			static void store_capture_into_argument_tuple_impl(std::index_sequence<i...>, const regex_match_result_type& match, function_argument_tuple& args) 
			{
				size_t j{ 1 };
				((std::get<i>(args) = std::get<j++>(match)), ...);
			}

			static void store_capture_into_argument_tuple(const regex_match_result_type& match, function_argument_tuple& args)
			{
				store_capture_into_argument_impl(std::make_index_sequence<capture_group_count>(), match, args);
			}

			/*
			static bool match_and_call(const route_context& ctx)
			{
				if (auto match{ ctre::match<ctre_string>(ctx.url.path()) })
				{
					function_argument_tuple args{};
					store_capture_into_argument_tuple(match, args);

					return true;
				}
				else
                    return false;
			}	*/
		};
	}

    template<typename ... endpoints>
	struct router
	{
		boost::asio::awaitable<response> route(request req)
		{
			[] <typename endpoint>(const request & req)
			{
				using descriptor = detail::route_descriptor<endpoint>;
				if (auto match{ ctre::match<descriptor::ctre_string>(ctx.url.path()) })
				{
					descriptor::function_argument_tuple args{};
					descriptor::store_capture_into_argument_tuple(match, args);

					return true;
				}
				else
                    return false;
			}
		}
	};
}
