#pragma once
#include "includes.h"
#include "defs.h"
#include "url_router.h"

template<Router router>
struct http_server
{
	asio::io_context& m_ctx;
	uint16_t m_port;
	asio::ip::address m_address{};

	router m_router;

	asio::awaitable<response> process_request(request req)
	{
		auto tgt{ req.target() };
		co_return co_await m_router.route(std::move(req), this, &m_ctx);
	}

	asio::awaitable<void> run_connection(tcp::socket socket)
	{
		beast::flat_buffer buffer;
		try {
			for (;;)
			{
				request req;
				co_await http::async_read(socket, buffer, req, use_awaitable);

				auto resp{ co_await process_request(std::move(req)) };

				http::serializer<false, response::body_type> sr{ resp };
				co_await http::async_write(socket, sr, use_awaitable);
				if (resp.need_eof())
					break;
			}
			socket.shutdown(asio::socket_base::shutdown_both);
		}
		catch (std::exception& ex)
		{
			handle_client_error(ex);
		}

		co_return;
	}

	http_server(asio::io_context& ctx, uint16_t port, asio::ip::address address = {})
		: m_ctx{ ctx }, m_port{ port }, m_address{ address }
	{}

	virtual void handle_client_error(std::exception& ex)
	{
	}

	virtual ~http_server()
	{}

	asio::awaitable<void> run_server_async()
	{
		tcp::acceptor acceptor{ m_ctx, tcp::endpoint{ m_address, m_port } };

		for (;;)
		{
			auto client_socket{ co_await acceptor.async_accept(m_ctx, use_awaitable) };
			co_spawn(m_ctx, run_connection(std::move(client_socket)), detached);
		}
	}
};

template<auto... routes>
using simple_http_server = http_server<router_t<routes...>>;
