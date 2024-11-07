#include <clopts.hh>
#include <print>

import pr.server;
import pr.tcp;
import pr.utils;

using namespace pr;
using namespace command_line_options;

using options = clopts< // clang-format off
    option<"--port", "The port to listen on", i64>,
    option<"--pwd", "Password to the game">,
    help<>
>; // clang-format on

int main(int argc, char* argv[]) {
    auto opts = options::parse(argc, argv);

    i64 port = opts.get_or<"--port">(net::DefaultPort);
    if (port <= 0 or port > std::numeric_limits<u16>::max()) {
        std::println(stderr, "ERROR: invalid port {}", port);
        return 1;
    }

    server::Server(u16(port), opts.get_or<"--pwd">("")).Run();
}
