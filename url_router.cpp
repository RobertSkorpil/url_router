#include "includes.h"
#include "defs.h"
#include "url_router.h"
#include "server.h"

get_endpoint<"/hello"> 
hello(asio::io_context *ctx, request *req) { 
	boost::asio::system_timer wait{ *ctx, 500ms };
	co_await wait.async_wait(use_awaitable);
	auto agent{ (*req)[http::field::user_agent] };
	co_return response{ http::status::ok, 11, std::format("ahoj, {}\n", std::string_view{ agent.begin(), agent.end() }) };
}

get_endpoint<"/hello2"> 
hello2(reroute_t reroute) { 
	co_return co_await reroute("/hello");
}

get_endpoint<"/div/<a>/<b>"> 
divide(path_arg<"b", uint32_t> b, path_arg<"a", uint32_t> a, query_arg<"x", uint32_t> x)
{
	if (!b)
		co_return response{ http::status::bad_request, 11, "to nejde\n" };

	auto d{ a / b };
	auto r{ a - d * b };
	co_return response{ http::status::ok, 11, std::format("x = {}\n {}, zbytek {}\n", x.value, d, r) };
}

any_endpoint<"*"> 
not_found() 
{ 
	co_return response{ http::status::not_found, 11, "nemame, nevedeme\n" }; 
}

get_endpoint<"/api/aa">
api_aa(reroute_t reroute) {
	co_return co_await reroute("/hello");
}

router_t<&api_aa, &not_found> subrouter;

get_endpoint<"/api/*"> 
api(asio::io_context *ctx, request *req, reroute_t reroute) {
	co_return co_await subrouter.route(*req, ctx, reroute);
}

v2::async_endpoint<verbs::get, "/api/*/x/**/div/<a>/<b>", response>
test_v2(path_arg<"a", uint32_t> a, path_arg<"b", std::string_view> b)
{
	co_return response{ http::status::not_found, 11, "nemame, nevedeme\n" }; 
}

int main()
{
	using test_route = v2::route<&test_v2>;
	std::string_view path_regex{ test_route::path_regex.str.begin(), test_route::path_regex.str.end() };
	std::println("{}", path_regex);
	std::println("{}", typeid(test_route::regex_match_result_type).name());
	std::println("{}", test_route::capture_group_count);

	boost::asio::io_context ctx;
	simple_http_server<&hello, &divide, &api, &not_found> srvr{ ctx, 3454 };

	std::jthread t{ [&] {ctx.run(); } };
	(void)getchar();
	ctx.stop();

	return 0;
}
