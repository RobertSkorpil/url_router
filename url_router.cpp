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
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/asio/high_resolution_timer.hpp>
#include <iostream>
#include "defs.h"
#include "url_router.h"

boost::asio::io_context ctx;

endpoint<"/product/<category>/<id>">
product(path_arg<"id", uint32_t> id, path_arg<"category", uint32_t> cat)
{
	boost::asio::high_resolution_timer timer{ ctx, 2s };
	co_await timer.async_wait(use_awaitable);
	co_return response{ http::status::ok, 11, "xxxx" };
}

struct S
{
    endpoint<"/category/<category>">
    category(path_arg<"category", uint32_t> cat, query_arg<"count", uint32_t> count)
    {
        co_return response{ http::status::ok, 11, "xxxx" };
    }
};

router_t<product, &S::category> router{};

int main()
{
	S s;
	boost::asio::co_spawn(ctx, router.route("/product/1/23", &s), detached);
	boost::asio::co_spawn(ctx, router.route("/category/1?count=312", &s), detached);

	std::jthread thread([&] { 
		ctx.run_for(10s); 
		});

	return 0;
}
