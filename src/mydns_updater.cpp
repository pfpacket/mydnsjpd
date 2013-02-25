//#define BOOST_ASIO_ENABLE_HANDLER_TRACKING

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <tuple>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <unistd.h>
#include <getopt.h>
#include <syslog.h>
#include <boost/asio.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>

namespace asio = boost::asio;
namespace fs = boost::filesystem;
using namespace std::placeholders;
enum class default_params : unsigned int {
    interval_sec = 60 * 60
};

struct mydns_update_opt {
    std::string config_file;
    std::string username, passwd;
    bool verbose = false, become_daemon = false;
    unsigned int interval = static_cast<unsigned int>(default_params::interval_sec);
};

void usage(std::string const& prog_name)
{
    std::cout
        << "Usage: " << prog_name << " [OPTION]... -f conf_file" << std::endl
        << "Options:" << std::endl
        << "  -d\t\tbecome a daemon" << std::endl
        << "  -f\t\tuse specified file as a config file" << std::endl
        << "  -v\t\tenable verbose output" << std::endl
        << "  -h\t\tdisplay this help and exit" << std::endl;
}

void die(std::string const& prog_name, int exit_code)
{
    usage(prog_name);
    std::exit(exit_code);
}

// config file format:
// USERNAME=user
// PASSWORD=password
// INTERVAL=sec
void read_config_file(mydns_update_opt& opt)
{
    std::ifstream ifs(opt.config_file);
    if (!ifs)
        throw std::runtime_error(opt.config_file + ": Could not open or no such file");
    for (std::string line; (std::getline(ifs, line)); ) {
        std::istringstream iss(line);
        std::string varname, content;
        std::getline(iss, varname, '=');
        std::getline(iss, content);
        if (varname == "USERNAME")
            opt.username = std::move(content);
        else if (varname == "PASSWORD")
            opt.passwd = std::move(content);
        else if (varname == "INTERVAL")
            opt.interval = static_cast<unsigned int>(std::atoi(content.c_str()));
        else if (varname.empty() || varname[0] != '#')
            throw std::runtime_error(opt.config_file + ": Invalid variable \'" + varname + '\'');
    }
}

void parse_cmd_line(int argc, char **argv, mydns_update_opt& opt)
{
    for (int c; (c = ::getopt(argc, argv, "df:vh")) != -1;) {
        switch (c) {
        case 'd':
            opt.become_daemon = true;
            break;
        case 'f':
            opt.config_file.assign(optarg);
            break;
        case 'v':
            opt.verbose = true;
            break;
        case 'h':
            die(argv[0], EXIT_SUCCESS);
            break;
        default:
            die(argv[0], EXIT_FAILURE);
        }
    }

    if (opt.config_file.empty())
        throw std::runtime_error("Invalid config file");
    opt.config_file = fs::absolute(opt.config_file).native();
    read_config_file(opt);
    if (opt.username.empty() || opt.passwd.empty())
        throw std::runtime_error("Username or password not specified");
}

void base64_encode(std::string const& from, std::string& to)
{
    using namespace boost::archive::iterators;
    typedef base64_from_binary<transform_width<decltype(from.begin()), 6, 8 > > base64_iterator;
    std::ostringstream oss;
    std::copy(
        base64_iterator(BOOST_MAKE_PFTO_WRAPPER(from.begin())),
        base64_iterator(BOOST_MAKE_PFTO_WRAPPER(from.end())),
        std::ostream_iterator<char>(oss)
    );
    to = std::move(oss.str());
}

std::tuple<std::string, int, std::string> mydns_update(mydns_update_opt const& opt)
{
    std::string basic_auth_str;
    base64_encode(opt.username + ':' + opt.passwd, basic_auth_str);
    boost::asio::ip::tcp::iostream s;
    s.expires_from_now(boost::posix_time::seconds(2));
    s.connect("www.mydns.jp", "80");
    if (!s)
        throw std::runtime_error(s.error().message());
    s   << "GET /login.html HTTP/1.1\r\n"
        << "Host: www.mydns.jp\r\n"
        << "Connection: close\r\n"
        << "Authorization: Basic " << basic_auth_str << "\r\n"
        << "\r\n" << std::flush;
    if (!s)
        throw std::runtime_error("unable to send the HTTP request");
    std::string http_version, http_status, status_msg;
    s >> http_version >> http_status;
    std::getline(s, status_msg);
    if (!s)
        throw std::runtime_error("unable to receive the HTTP response");
    if (opt.verbose && !opt.become_daemon)
        std::copy(
            std::istreambuf_iterator<char>(s.rdbuf()),
            std::istreambuf_iterator<char>(),
            std::ostreambuf_iterator<char>(std::cout.rdbuf())
        );
    return std::make_tuple(http_version, std::atoi(http_status.c_str()), status_msg.substr(1));
}

class mydns_daemon {
public:
    typedef asio::basic_waitable_timer<std::chrono::steady_clock> steady_timer;
    mydns_daemon(asio::io_service& io, mydns_update_opt& opt)
        : io_(io), sigset_(io, SIGHUP, SIGINT, SIGTERM), timer_(io), opt_(opt)
    {
        io_.notify_fork(asio::io_service::fork_prepare);
        if (::daemon(0, 0) < 0)
            die("daemon(): " + std::string(std::strerror(errno)), EXIT_FAILURE);
        io_.notify_fork(asio::io_service::fork_child);
        start_service();
        ::openlog("mydns_update", LOG_CONS | LOG_PID, LOG_DAEMON);
        ::syslog(LOG_INFO, "daemon started masterID=%s,interval(sec)=%d", opt_.username.c_str(), opt_.interval);
    }
    ~mydns_daemon()
    {
        ::syslog(LOG_INFO, "daemon now finished");
        ::closelog();
    }

private:
    void start_service()
    {
        sigset_.async_wait(std::bind(&mydns_daemon::sighandler, this, _1, _2));
        timer_.expires_from_now(std::chrono::seconds(opt_.interval));
        timer_.async_wait(std::bind(&mydns_daemon::timer_handler, this, _1));
    }

    void timer_handler(boost::system::error_code const& err)
    {
        if (err)
            ::syslog(LOG_ERR, "Error: timer handler: %s", err.message().c_str());
        else try {
            auto ret = mydns_update(opt_);
            ::syslog(std::get<1>(ret) == 200 ? LOG_NOTICE : LOG_ERR,
                "status=%d msg=%s", std::get<1>(ret), std::get<2>(ret).c_str());
        } catch (std::exception& e) {
            ::syslog(LOG_ERR, "Exception: timer handler: %s", e.what());
        }
        timer_.expires_from_now(std::chrono::seconds(opt_.interval));
        timer_.async_wait(std::bind(&mydns_daemon::timer_handler, this, _1));
    }

    void sighandler(boost::system::error_code const& err, int signum)
    {
        if (err)
            ::syslog(LOG_ERR, "Error: signal hander: %s", err.message().c_str());
        else if (signum == SIGHUP) try {
            read_config_file(opt_);
            ::syslog(LOG_INFO, "Reload config: masterID=%s,interval(sec)=%d", opt_.username.c_str(), opt_.interval);
        } catch (std::exception& e) {
            ::syslog(LOG_ERR, "Exception: signal handler: %s", e.what());
        } else if (signum == SIGINT || signum == SIGTERM) {
            ::syslog(LOG_NOTICE, "Received SIGINT or SIGTERM, now stopping operations");
            io_.stop();
        }
        sigset_.async_wait(std::bind(&mydns_daemon::sighandler, this, _1, _2));
    }

    asio::io_service& io_;
    asio::signal_set sigset_;
    steady_timer timer_;
    mydns_update_opt& opt_;
};

void io_service_run(asio::io_service& io_service)
{
    for (;;) {
        try {
            io_service.run();
            break;
        } catch (std::exception& e) {
            ::syslog(LOG_ERR, "Exception: catched by io_service_run(): %s", e.what());
        }
    }
}

int main(int argc, char **argv)
{
    int exit_code = EXIT_SUCCESS;
    try {
        if (argc < 2)
            throw std::runtime_error("Too few arguments, use -h option to display usage");
        mydns_update_opt opt;
        parse_cmd_line(argc, argv, opt);
        if (opt.become_daemon) {
            asio::io_service io_service;
            mydns_daemon daemon(io_service, opt);
            io_service_run(io_service);
        } else {
            auto ret = mydns_update(opt);
            int return_code = std::get<1>(ret);
            if (return_code != 200)
                throw std::runtime_error(std::to_string(return_code) + " / " + std::get<2>(ret));
            std::cout << "status=" << return_code << " msg=" << std::get<2>(ret) << std::endl;
        }
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        exit_code = EXIT_FAILURE;
    }
    return exit_code;
}
