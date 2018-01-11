// Assign one of Example values to EXAMPLE_TO_BUILD to choose what to build.
enum class Example {
    SimpleStream, Socket, Multithreaded, Strand, 
    Future, Coroutines, // these two are not working!
    Callbacks
};
constexpr Example EXAMPLE_TO_BUILD = Example::Callbacks; // <--- select what to build here


#include "experimental/net"
#include <chrono>
#include <future>
#include <iostream>
#include <regex>
#include <string>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>

namespace net = std::experimental::net;
using namespace std::chrono_literals;
using namespace std::string_literals;

//==============================================================================
int main(int, char*[])
{
    static const auto domain = "localhost"s;//"www.boost.org"s;
    static const uint16_t port = 5984;

    //--------------------------------------------------------------------------
    if constexpr (EXAMPLE_TO_BUILD == Example::SimpleStream)
    {
        // Simple synchronous HTTP stream reading
        net::ip::tcp::iostream s;
        s.expires_after(5s);
        s.connect(domain, "http");

        if (!s) {
            std::cerr << "Error: " << s.error().message() << std::endl;
            return -1;
        }

        s << "GET / HTTP/1.0\r\n";
        s << "Host: "s+ domain +"\r\n"s;
        s << "Accept: */*\r\n";
        s << "Connection: close\r\n\r\n";

        std::string header;
        while (std::getline(s, header) && header != "\r") {
            std::cout << header << "\n";
        }
        std::cout << "All good!\n"; // s.rdbuf();
    }
    //--------------------------------------------------------------------------
    if constexpr (EXAMPLE_TO_BUILD == Example::Socket)
    {
        // Synchronous HTTP operation with socket
        net::io_context io_context;
        net::ip::tcp::socket   socket(io_context);

        net::ip::tcp::resolver resolver(io_context);
        net::connect(socket, resolver.resolve(domain, std::to_string(port)));

        for (auto v : { "GET /_all_dbs HTTP/1.0\r\n"s
                      , "Host: "s + domain + ":"+ std::to_string(port) +"\r\n"s
                      , "Accept: */*\r\n"s
                      , "Connection: close\r\n\r\n"s })
        {
            net::write(socket, net::buffer(v));
        }

        // Read the response header first
        static const auto HEADER_END = "\r\n\r\n"s;
        std::string header_buf;
        net::read_until(socket, net::dynamic_buffer(header_buf), HEADER_END);

        // Now, find header_end in the header buffer (as header_buf
        // likely contains more than just the response header).
        const auto header_end_pos = header_buf.find(HEADER_END);
        if (header_buf.empty() || header_end_pos == std::string::npos) {
            std::cerr << "Error: cannot find response delimiter in \""<< header_buf <<"\""<< std::endl;
            return 1;
        }
        assert(header_end_pos + HEADER_END.length() <= header_buf.length());

        // string_view "header" contains only the response header
        std::string_view header(&header_buf[0], header_end_pos);
        std::cout << header << "\n\n";

        // Read the rest (the end of header_buf is likely to contain beginning of the response body).
        std::error_code e;
        std::string body;
        net::read(socket, net::dynamic_buffer(body), e);

        // "end of file" error is normal
        if (const auto error_code = e.value(); error_code != 0 && error_code != net::stream_errc::eof) {
            std::cerr << "Error "<< error_code <<": \""<< e.message() <<"\""<< std::endl;
        }

        // Insert the end of header_buf (the beginning of the response body).
        const auto body_beginning = header_buf.substr(header_end_pos + HEADER_END.length());
        if (!body_beginning.empty()) {
            body.insert(0, body_beginning);
        }

        std::cout << body << std::endl; 
    }
    //--------------------------------------------------------------------------
    if constexpr (EXAMPLE_TO_BUILD == Example::Multithreaded)
    {
        // Work posting and async execution on multiple threads
        net::io_context ctx;
        auto work = net::make_work_guard(ctx);

        std::thread t1 = std::thread([&ctx]() { ctx.run(); });
        std::thread t2 = std::thread([&ctx]() { ctx.run(); });

        net::post(ctx, [](){ std::this_thread::sleep_for(4s); std::cout << "Task 1\n"; });
        net::post(ctx, [](){ std::this_thread::sleep_for(2s); std::cout << "Task 2\n"; });

        work.reset();

        t1.join();
        t2.join();
    }
    //--------------------------------------------------------------------------
    if constexpr (EXAMPLE_TO_BUILD == Example::Strand)
    {
        // Work posting and async execution on single thread using a strand
        net::io_context ctx;
        auto work = net::make_work_guard(ctx);

        net::strand<net::io_context::executor_type> strand(ctx.get_executor());

        std::thread t1 = std::thread([&ctx]() { ctx.run(); });
        std::thread t2 = std::thread([&ctx]() { ctx.run(); });

        net::post(strand, [](){ std::this_thread::sleep_for(3s); std::cout << "Task 1\n"; });
        net::post(strand, [](){ std::this_thread::sleep_for(1s); std::cout << "Task 2\n"; });

        work.reset();

        t1.join();
        t2.join();
    }
    //--------------------------------------------------------------------------
    if constexpr (EXAMPLE_TO_BUILD == Example::Future)
    {
        using net::ip::tcp;
        net::io_context io_context;
        std::thread t([&io_context]() {io_context.run(); });

        auto resolver = tcp::resolver(io_context);
        auto resolve = resolver.async_resolve(domain, "http", net::use_future);

        // >>> resolve.get() hangs ...
        tcp::socket socket(io_context);
        auto connect = net::async_connect(socket, resolve.get(), net::use_future);

        auto request = "GET / HTTP/1.0\r\nHost: "s+ domain +"\r\nAccept: */*\r\nConnection: close\r\n\r\n"s;
        socket.async_send(net::buffer(request), [](auto, auto) {});

        std::string header;
        auto header_read = net::async_read_until(socket, net::dynamic_buffer(header), "\r\n\r\n", net::use_future);
        // do some stuff ....
        if (header_read.get() <= 2) { std::cout << "no header\n"; }
        std::cout << "Header:\n" << header;

        std::string body;
        auto body_read = net::async_read(socket, net::dynamic_buffer(body),
            net::transfer_all(),
            net::use_future);
        // do some stuff ....
        body_read.get();
        std::cout << "Body:\n" << body << "\n";

        io_context.stop();
        t.join();
    }
    //--------------------------------------------------------------------------
    if constexpr (EXAMPLE_TO_BUILD == Example::Coroutines)
    {
    }
    //--------------------------------------------------------------------------
    if constexpr (EXAMPLE_TO_BUILD == Example::Callbacks)
    {
        static const std::string HEADER_END = "\r\n\r\n"s;
        class http_getter
        {
        public:
            explicit http_getter(net::io_context& io_context)
                : m_io_context(io_context)
                , m_socket    (io_context)
            {}

            struct response_data {
                int         code = 0;
                std::string header;
                std::string body;
            };
            std::future<response_data> get_page(const std::string& host, const std::string& resource = "/", uint16_t port = 80)
            {
                m_promise = std::promise<response_data>{};
                m_request = "GET "s+ resource +" HTTP/1.0\r\nHost: "s+ host +"\r\nAccept: */*\r\nConnection: close\r\n\r\n"s;
                connect(host, std::to_string(port));
                return m_promise.get_future();
            }

        private:
            void connect(const std::string& host, const std::string& port)
            {
                net::ip::tcp::resolver resolver(m_io_context);
                net::async_connect(m_socket, resolver.resolve(host, port),
                    [this, host](auto ec, auto end_point) {
                    if (!ec) {
                        m_socket.async_send(net::buffer(m_request),
                            [this](auto ec, auto bytes_sent) {
                            if (!ec) {
                                read_header();
                            } else {
                                m_promise.set_exception(std::make_exception_ptr(ec));
                            }
                        });
                    } else {
                        m_promise.set_exception(std::make_exception_ptr(ec));
                    }
                });
            }

            void read_header()
            {
                net::async_read_until(m_socket, net::dynamic_buffer(m_header), HEADER_END,
                    [this](auto ec, std::size_t bytes_in_header) {
                    if (!ec) {
                        assert(m_header.find(HEADER_END) + HEADER_END.length() == bytes_in_header && bytes_in_header <= m_header.length());

                        int response_code = 0;
                        if (std::smatch m; std::regex_search(m_header, m, std::regex(R"=(^HTTP\/\d\.\d\s(\d+).*)=")) && m.size() == 2) {
                            response_code = std::stoi(m[1].str());
                        }

                        read_body(bytes_in_header, response_code);
                    } else {
                        m_promise.set_exception(std::make_exception_ptr(ec));
                    }
                });
            }

            void read_body(std::size_t bytes_in_header, int response_code)
            {
                net::async_read(m_socket, net::dynamic_buffer(m_body),
                    net::transfer_all(),
                    [this, bytes_in_header, response_code](auto ec, auto bytes_trans) {
                    if (!ec || ec == net::stream_errc::eof) // "end of file" error is expected
                    {
                        // Insert the end of header_ at the beginning of body_ 
                        if (bytes_in_header < m_header.length()) {
                            m_body.insert(0, m_header.substr(bytes_in_header));
                        }
                        m_promise.set_value(response_data{ 
                            response_code,
                            std::move(m_header.substr(0, bytes_in_header - HEADER_END.length())),
                            std::move(m_body) });
                    } else {
                        m_promise.set_exception(std::make_exception_ptr(ec));
                    }
                    std::error_code close_ec;
                    m_socket.close(close_ec);
                });
            }

        //data:
            net::io_context&     m_io_context;
            net::ip::tcp::socket m_socket;
            std::string          m_request;
            std::string          m_header;
            std::string          m_body;
            std::promise<response_data> m_promise;
        };

        
        net::io_context io_context;

        auto work = net::make_work_guard(io_context);
        std::thread t = std::thread([&io_context]() { io_context.run(); });

        http_getter hg(io_context);
        auto future = hg.get_page(domain, "/_all_dbs", port);
        try {
            auto [code, header, body] = future.get();
            std::cout << "HTTP response code: " << code
                      << "\n====================================\n"
                      << "Header:\n" << header
                      << "\n====================================\n"
                      << "Body:\n" << body << std::endl;
        } catch (const std::error_code& e) {
            std::cerr << "Error "<< e.value() <<": \""<< e.message() <<"\"" << std::endl;
        }

        io_context.stop();
        t.join();
   }
    //--------------------------------------------------------------------------
    return 0;
}