#include <Client/Client.hh>

#include <base/Macros.hh>

#include <clopts.hh>
#include <print>

using namespace pr;
using namespace command_line_options;

using options = clopts< // clang-format off
    option<"--connect", "The server IP to connect to">,
    option<"--name", "The name to set for us">,
    option<"--password", "The password to use for login">,
    help<>
>; // clang-format on

static auto SetUpPath() -> Result<> {
    // Try to cd into the executable directory if we’re not already there;
    // to make sure we can resolve relative paths properly.
    return fs::ChangeDirectory(Try(fs::ExecutablePath()).parent_path());
}

int main(int argc, char** argv) {
    auto opts = options::parse(argc, argv);

    if (auto res = SetUpPath(); not res)
        Log("Failed to set up path: {}", res.error());

    if (opts.get<"--connect">()) {
        if (not opts.get<"--name">() or not opts.get<"--password">()) {
            std::println(
                stderr,
                "If --connect is used, --name and --password must also be provided"
            );

            return 1;
        }

        client::Client::RunAndConnect(
            *opts.get<"--connect">(),
            *opts.get<"--name">(),
            *opts.get<"--password">()
        );
        return 0;
    }

    // Run the client.
    client::Client::Run();
}
