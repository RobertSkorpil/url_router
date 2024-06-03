#include "pch.h"
#include "defs.h"
#include "resource.h"
#include <resource_view.h>
#include "api.h"
#include "handle.h"
#include "global.h"
#include "single_instance.h"
#include "url_router.h"

namespace po = boost::program_options;
namespace
{
	resource_view www_resrc{ WWWFILES };
	resource_view database_resrc{ DATABASE };
	archive_view wwwfiles{ www_resrc.image() };
	archive_view database{ database_resrc.image() };

	enum verbose_mode_t { normal, verbose };

	struct http_server
	{
		uint16_t m_port;
		verbose_mode_t m_verbose_mode;

		asio::io_context m_ctx{ 1 };
		std::optional<std::jthread> m_thread;

		void run_context()
		{
			m_ctx.run();
		}

		asio::awaitable<void> run_server()
		{
			auto address{ asio::ip::make_address("127.0.0.1") };
			tcp::acceptor acceptor{ m_ctx, tcp::endpoint{ address, m_port } };

			for (;;)
			{
				auto client_socket{ co_await acceptor.async_accept(m_ctx, use_awaitable) };
				co_spawn(m_ctx, run_connection(std::move(client_socket)), detached);
			}
		}

		template<literal L>
		using endpoint = endpoint<L, response>;

		endpoint<"/api/<*>">
		serve_api(url_arg url)
		{
			return api_call(url->path());
		}

		endpoint<"<*>">
		serve_file(url_arg url)
		{
			auto data{ [&,url=url->path()]() {
				if (auto [match, file] { ctre::match<R"(^/data/(.*)$)">(url) }; match)
					return database.get_file(file);
				else
					return wwwfiles.get_file(url.substr(1));
				}() };

			if (!data)
				return response{ http::status::not_found, 11 };
			else
			{
				response resp{ http::status::ok, 11 };
				resp.body() = { data->begin(), data->end() };
				return resp;
			}
		}

		router_t<&serve_api, &serve_file> router;

		asio::awaitable<response> process_request(const request& req)
		{
			auto tgt{ req.target() };
			co_return router.route(req.target(), this);
		/*
			if(tgt.starts_with("/api/"))
				return server_api(tgt);
			else
			{
				if (ctre::match<R"(^()|(/[\d+]?)|(/product/.*)|(/order.*)$)">(tgt))
					tgt = "/index.html";

				return serve_file(tgt);
			}	*/
		}

		void dump(const request& req, fmt::color color = fmt::color::cyan)
		{
			fmt::print(fmt::fg(color), "{} {}\n", req.method_string(), req.target());
			for (const auto& header : req)
				fmt::print(fmt::fg(color), "{}: {}\n", header.name_string(), header.value());
		}

		void dump(const response& resp, fmt::color color = fmt::color::orchid)
		{
			fmt::print(fmt::fg(color), "HTTP {} {}\n", resp.result_int(), resp.reason());
			for (const auto& header : resp)
				fmt::print(fmt::fg(color), "{}: {}\n", header.name_string(), header.value());

			if (auto i{ resp.find(boost::beast::http::field::content_type) }; i != resp.end() && i->value() == "application/json")
				fmt::print(fmt::fg(color), "\n{}\n\n", resp.body());
		}

		asio::awaitable<void> run_connection(tcp::socket socket)
		{
			beast::flat_buffer buffer;
			try {
				for (;;)
				{
					request req;
					co_await http::async_read(socket, buffer, req, use_awaitable);

					if (m_verbose_mode == verbose_mode_t::verbose)
						dump(req);

					auto resp{ co_await process_request(req) };

					if (m_verbose_mode == verbose_mode_t::verbose)
						dump(resp);

					http::serializer<false, response::body_type> sr{ resp };
					co_await http::async_write(socket, sr, use_awaitable);
					if (resp.need_eof())
						break;
				}
				socket.shutdown(asio::socket_base::shutdown_both);
			}
			catch (std::exception& ex)
			{
				fmt::print(fmt::fg(fmt::color::orange_red), "Error: {0}\n", ex.what());
			}

			co_return;
		}

		http_server(uint16_t port, verbose_mode_t verbose_mode)
			: m_port{ port }, m_verbose_mode{ verbose_mode }
		{
			co_spawn(m_ctx, run_server(), detached);
			m_thread.emplace(std::bind(&http_server::run_context, this));
		}

		~http_server()
		{
			m_ctx.stop();
		}
	};

	void run_browser(std::string_view url)
	{
		std::string urls{ url };
		ShellExecuteA(nullptr, nullptr, urls.c_str(), nullptr, nullptr, SW_SHOWMAXIMIZED);
	}
	po::options_description options {
		[] {
			po::options_description opts;
			opts.add_options()
				("help,h", po::bool_switch()->default_value(false), "Show help")
				("port,p", po::value<uint16_t>()->default_value(5556), "List on specified port")
				("nobrowse,n", po::bool_switch()->default_value(false), "Don't open browser")
				("development,d", po::bool_switch()->default_value(false), "Development mode")
				("verbose,v", po::bool_switch()->default_value(false), "Verbose mode");
			
			return opts;
			}()
	};
}

int main(int argc, char **argv)
{
	po::variables_map vm;
	try {
		auto opts{ po::command_line_parser(argc, argv).options(options).run() };
		po::store(opts, vm);
		po::notify(vm);
	}
	catch (po::error& ex)
	{
		fmt::print("Invalid syntax: {}\n", ex.what());
		options.print(std::cout);
		return 1;
	}

	if (vm["help"].as<bool>())	
		options.print(std::cout);
	else
	{
		port = vm["port"].as<uint16_t>();
		development_mode = vm["development"].as<bool>() ? development_mode_t::development : development_mode_t::normal;

		std::optional<single_instance> si;
		try { si.emplace(); }
		catch(already_running_error &err) {
			fmt::print("{}\n", err.what());
			return 1;
		}

		http_server server{ port, vm["verbose"].as<bool>() ? verbose_mode_t::verbose : verbose_mode_t::normal };
		fmt::print("Server is running on http://localhost:{}/.\nPress ENTER to terminate\n", server.m_port);

		if(!vm["nobrowse"].as<bool>())
			run_browser(fmt::format("http://localhost:{}/", server.m_port));

		si->wait();
	}

	return 0;
}
