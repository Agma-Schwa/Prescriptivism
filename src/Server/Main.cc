#include <clopts.hh>
#include <print>

import pr.server;
import pr.tcp;
import pr.utils;

using namespace pr;
using namespace command_line_options;

using options = clopts<
        option<"--port", "The port to listen too", i64>,
        option<"--pwd", "Password to the game">,
        help<>
    >;

int main(int argc, char* argv[]) {
    auto opts = options::parse(argc, argv);
    i64 port = opts.get_or<"--port">(net::DefaultPort);
    if (port <= 0 or port > std::numeric_limits<u16>::max()) {
        std::println(stderr,"ERROR: invalid port {}", port);
        return 1;
    }
    auto pwd = opts.get_or<"--pwd">("");
    server::Server(u16(port), std::move(pwd)).Run();
}
