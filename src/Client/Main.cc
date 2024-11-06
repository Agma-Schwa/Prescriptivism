#include <base/Macros.hh>

import pr.client;
import pr.utils;

using namespace pr;

static auto SetUpPath() -> Result<> {
    // Try to cd into the executable directory if we’re not already there;
    // to make sure we can resolve relative paths properly.
    return fs::ChangeDirectory(Try(fs::ExecutablePath()).parent_path());
}

int main() {
    if (auto res = SetUpPath(); not res)
        Log("Failed to set up path: {}", res.error());

    // Run the client.
    client::Client::Run();
}
