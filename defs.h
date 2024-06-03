#pragma once

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using asio::co_spawn;
using asio::use_awaitable;
using asio::detached;
using tcp = asio::ip::tcp;
using request = http::request<http::string_body>;
using response = http::response<http::string_body>;
using namespace std::literals;
