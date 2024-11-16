#include <print>
#include <sys/wait.h>

#ifndef __linux__
int main() {
    std::println(stderr, "Sorry, not supported on this platform");
    return 1;
}
#else
#    include <csignal>
#    include <unistd.h>
#    include <utility>

[[noreturn]] void Kill(int = 0) {
    killpg(0, SIGKILL);
    _Exit(42);
}

int main(int argc, char** argv) {
    setpgid(0, 0);

    std::atexit([] { Kill(); });
    signal(SIGINT, Kill);
    signal(SIGTERM, Kill);
    signal(SIGHUP, Kill);
    signal(SIGQUIT, Kill);
    signal(SIGSEGV, Kill);
    signal(SIGABRT, Kill);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    // Do not rebuild ourselves as that may cause a crash.
    std::system("cmake --build out -- PrescriptivismServer Prescriptivism");

    if ((argc < 2 or std::string_view{argv[1]} != "-c") and fork() == 0) {
        execl(
            "./PrescriptivismServer",
            "PrescriptivismServer",
            "--pwd",
            "password",
            nullptr
        );
        abort();
    }

    if (fork() == 0) {
        execl(
            "./Prescriptivism",
            "Prescriptivism",
            "--connect",
            "localhost",
            "--name",
            "testuser1",
            "--password",
            "password",
            nullptr
        );
        abort();
    }

    if ((argc < 2 or std::string_view{argv[1]} != "-1") and fork() == 0) {
        execl(
            "./Prescriptivism",
            "Prescriptivism",
            "--connect",
            "localhost",
            "--name",
            "testuser2",
            "--password",
            "password",
            nullptr
        );
        abort();
    }

    waitpid(0, nullptr, 0);
    waitpid(0, nullptr, 0);
    waitpid(0, nullptr, 0);
    std::println("All children have exited normally");
    return 0;
}
#endif
