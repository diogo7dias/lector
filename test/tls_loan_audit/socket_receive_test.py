"""Compile the actual wolfSSL receive callback against POSIX sockets on the host.

Only its WiFiClient fd accessor and wolfSSL error names are stand-ins; recv,
short reads, EAGAIN, EOF and EBADF run against real sockets. Extracting this
small function avoids stubbing the entire Arduino/wolfSSL handshake API.
"""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = (root / "freeink-sdk/libs/network/SecureNet/src/SecureClient.cpp").read_text()
start = source.index("int wcRecv(")
end = source.index("\n}\n", start) + 2
callback = source[start:end]
program = r'''
#include <cassert>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
struct WOLFSSL {};
enum { WOLFSSL_CBIO_ERR_GENERAL = -1, WOLFSSL_CBIO_ERR_WANT_READ = -2,
       WOLFSSL_CBIO_ERR_CONN_CLOSE = -5 };
struct WiFiClient {
  int socket;
  int fd() const { return socket; }
  // Deliberately no read(): regression to Arduino's heap buffer won't compile.
};
''' + callback + r'''
int main() {
  int sockets[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  WiFiClient client{sockets[0]};
  char record[32] = {};
  assert(wcRecv(nullptr, record, sizeof(record), &client) == WOLFSSL_CBIO_ERR_WANT_READ);
  assert(send(sockets[1], "abcdef", 6, 0) == 6);
  assert(wcRecv(nullptr, record, 2, &client) == 2);
  assert(memcmp(record, "ab", 2) == 0);
  assert(wcRecv(nullptr, record, sizeof(record), &client) == 4);
  assert(memcmp(record, "cdef", 4) == 0);
  assert(wcRecv(nullptr, record, sizeof(record), &client) == WOLFSSL_CBIO_ERR_WANT_READ);
  // Data queued before FIN must still be delivered, then EOF must stop retrying.
  assert(send(sockets[1], "last", 4, 0) == 4);
  close(sockets[1]);
  assert(wcRecv(nullptr, record, sizeof(record), &client) == 4);
  assert(memcmp(record, "last", 4) == 0);
  assert(wcRecv(nullptr, record, sizeof(record), &client) == WOLFSSL_CBIO_ERR_CONN_CLOSE);
  close(sockets[0]);
  client.socket = -1;
  assert(wcRecv(nullptr, record, sizeof(record), &client) == WOLFSSL_CBIO_ERR_GENERAL);
}
'''
with tempfile.TemporaryDirectory(prefix="tls-receive-") as tmp:
    cpp = pathlib.Path(tmp) / "receive.cpp"
    exe = pathlib.Path(tmp) / "receive"
    cpp.write_text(program)
    subprocess.run([sys.argv[1], "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("TLS socket receive: short reads, EAGAIN, queued data + EOF, fatal fd: PASS")
