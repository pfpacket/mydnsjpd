
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <tuple>
#include <stdexcept>
#include <getopt.h>
#include <boost/asio.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>

struct mydns_update_opt {
    std::string username, passwd;
    bool verbose = false;
};

void usage(std::string const& prog_name)
{
    std::cout
        << "Usage: " << prog_name << " [options] -f conf_file" << std::endl
        << "  -f        use specified file as a config file" << std::endl
        << "  -v        enable verbose output" << std::endl
        << "  -h        display this help and exit" << std::endl;
}

void die(std::string const& prog_name, int exit_code)
{
    usage(prog_name);
    std::exit(exit_code);
}

// config file format:
// USERNAME=user
// PASSWORD=password
void read_config_file(std::string const& config_file, mydns_update_opt& opt)
{
    std::ifstream ifs(config_file);
    if (!ifs)
        throw std::runtime_error(config_file + ": Could not open or no such file");
    for (std::string line; (std::getline(ifs, line)); ) {
        std::istringstream iss(line);
        std::string varname, content;
        std::getline(iss, varname, '=');
        std::getline(iss, content);
        if (varname == "USERNAME")
            opt.username = std::move(content);
        else if (varname == "PASSWORD")
            opt.passwd = std::move(content);
        else if (varname.empty() || varname[0] != '#')
            throw std::runtime_error(config_file + ": Invalid variable \'" + varname + '\'');
    }
}

void parse_cmd_line(int argc, char **argv, mydns_update_opt& opt)
{
    std::string config_file;
    for (int c; (c = ::getopt(argc, argv, "f:vh")) != -1;) {
        switch (c) {
        case 'f':
            config_file.assign(optarg);
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

    if (config_file.empty())
        throw std::runtime_error("Invalid config file");
    read_config_file(config_file, opt);
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
        << "\r\n";
    std::string http_version, http_status, status_msg;
    s >> http_version >> http_status >> status_msg;
    if (opt.verbose)
        std::copy(
            std::istreambuf_iterator<char>(s.rdbuf()),
            std::istreambuf_iterator<char>(),
            std::ostreambuf_iterator<char>(std::cout.rdbuf())
        );
    if (http_status != "200")
        throw std::runtime_error("remote host returned error code, " + http_status);
    return std::make_tuple(http_version, std::atoi(http_status.c_str()), status_msg);
}

int main(int argc, char **argv)
{
    int exit_code = EXIT_SUCCESS;
    try {
        if (argc < 2)
            throw std::runtime_error("Too few arguments, use -h option to display usage");
        mydns_update_opt opt;
        parse_cmd_line(argc, argv, opt);
        auto ret = mydns_update(opt);
        std::cout << "status=" << std::get<1>(ret) << " msg=" << std::get<2>(ret) << std::endl;
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        exit_code = EXIT_FAILURE;
    }
    return exit_code;
}
