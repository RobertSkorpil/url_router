#include "includes.h"
#include "defs.h"
#include "url_router.h"
#include "server.h"

get_endpoint<"/hello"> hello(asio::io_context *ctx, request *req) { 
	boost::asio::system_timer wait{ *ctx, 500ms };
	co_await wait.async_wait(use_awaitable);
	auto agent{ (*req)[http::field::user_agent] };
	co_return response{ http::status::ok, 11, std::format("ahoj, {}\n", std::string_view{ agent.begin(), agent.end() }) };
}

get_endpoint<"/div/<a>/<b>"> divide(path_arg<"a", uint32_t> a, path_arg<"b", uint32_t> b)
{
	if (!b)
		co_return response{ http::status::bad_request, 11, "to nejde\n" };

	auto d{ a / b };
	auto r{ a - d * b };
	co_return response{ http::status::ok, 11, std::format("{}, zbytek {}\n", d, r) };
}

any_endpoint<"*"> not_found() 
{ 
	co_return response{ http::status::not_found, 11, "nemame, nevedeme\n" }; 
}

int main()
{
	boost::asio::io_context ctx;
	simple_http_server<&hello, &divide, &not_found> srvr{ ctx, 3454 };
	boost::asio::co_spawn(ctx, srvr.run_server_async(), detached);

	std::jthread t{ [&] {ctx.run(); } };
	getchar();
	ctx.stop();

	return 0;
}
