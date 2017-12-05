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

#if (1) //(EXAMPLE_TO_BUILD == Example::Callbacks)
class web_page_getter
{
public:
    explicit web_page_getter(net::io_context& io_context)
        : io_context_(io_context)
        , socket_(io_context)
    {}

    struct response_data {
        int         code = 0;
        std::string header;
        std::string body;
    };
    std::future<response_data> get_page(const std::string& host, const std::string& port)
    {
        promise_ = std::promise<response_data>{};
        request_ = "GET / HTTP/1.0\r\nHost: "s + host + "\r\nAccept: */*\r\nConnection: close\r\n\r\n"s;
        connect(host, port);
        return promise_.get_future();
    }

private:
    void connect(const std::string& host, const std::string& port)
    {
        net::ip::tcp::resolver resolver(io_context_);
        net::async_connect(socket_, resolver.resolve(host, port), 
            [this, host](auto ec, auto end_point) {
                if (!ec) {
                    socket_.async_send(net::buffer(request_), 
                        [this](auto ec, auto bytes_sent) {
                            if (!ec) {
                                read_header();
                            } else {
                                promise_.set_exception(std::make_exception_ptr(ec));
                            }
                        });
                } else {
                    promise_.set_exception(std::make_exception_ptr(ec));
                }
            });
    }

    void read_header()
    {
        net::async_read_until(socket_, net::dynamic_buffer(header_), HEADER_END,
            [this](auto ec, std::size_t bytes_in_header) {
                if (!ec) 
                {
                    assert(header_.find(HEADER_END) + HEADER_END.length() == bytes_in_header && bytes_in_header <= header_.length());
                    // Extract the HTTP response code from the 1st line
                    if (std::smatch m; std::regex_search(header_, m, std::regex(R"=(^HTTP\/\d\.\d\s(\d+).*)=")) && m.size() == 2) {
                        response_code_ = std::stoi(m[1].str());
                    }

                    read_body(bytes_in_header);
                } else {
                    promise_.set_exception(std::make_exception_ptr(ec));
                }
            });
    }

    void read_body(std::size_t bytes_in_header)
    {
        net::async_read(socket_, net::dynamic_buffer(body_),
            net::transfer_all(),
            [this, bytes_in_header](auto ec, auto bytes_trans) {
                if (!ec || ec == net::stream_errc::eof) // "end of file" error is expected
                {
                    // Insert the end of header_ at the beginning of body_ 
                    // (slightly inefficient but shouldn't be too bad).
                    if (bytes_in_header < header_.length()) {
                        body_.insert(0, header_.substr(bytes_in_header));
                    }

                    promise_.set_value( response_data{ response_code_,
                                                       std::move(header_.substr(0, bytes_in_header - HEADER_END.length())), 
                                                       std::move(body_)} );
                } else {
                    promise_.set_exception(std::make_exception_ptr(ec));
                }
                std::error_code close_ec;
                socket_.close(close_ec);
            });
    }

//data:
    static const std::string HEADER_END;// = "\r\n\r\n"s;
    net::io_context&     io_context_;
    net::ip::tcp::socket socket_;
    std::string          request_;
    int                  response_code_ = 0;
    std::string          header_;
    std::string          body_;
    std::promise<response_data> promise_;
};
const std::string web_page_getter::HEADER_END = "\r\n\r\n"s;
#endif

//==============================================================================
int main(int, char*[])
{
    static const auto domain = "www.boost.org"s;

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
        net::connect(socket, resolver.resolve(domain, "http"));

        for (auto v : { "GET / HTTP/1.0\r\n"s
                      , "Host: "s + domain + "\r\n"s
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
        assert(header_end_pos + HEADER_END.length() < header_buf.length());

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
        net::io_context io_context;

        auto work = net::make_work_guard(io_context);
        std::thread t = std::thread([&io_context]() { io_context.run(); });

        web_page_getter wpg(io_context);

        auto f = wpg.get_page(domain, "http");
        try {
            auto [code, header, body] = f.get();
            std::cout 
                << "HTTP response code: " << code
                << "\n====================================\n"
                << "Header:\n" << header
                << "\n====================================\n"
                << "Body:\n" << body << std::endl;
        }
        catch (const std::error_code& e) {
            std::cerr << "Error "<< e.value() <<": \""<< e.message() <<"\"" << std::endl;
        }

        io_context.stop();
        t.join();
   }
    //--------------------------------------------------------------------------
    return 0;
}