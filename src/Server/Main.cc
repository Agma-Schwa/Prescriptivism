import pr.server;
import pr.tcp;

int main() {
    pr::server::Server(pr::net::DefaultPort).Run();
}
