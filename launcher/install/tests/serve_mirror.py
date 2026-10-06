"""A directory served over HTTPS with HTTP/1.1, for runners_updated.sh
(D414): the certificate and key in KEYS, the port chosen by the system and
written to PORT once listening.

usage: serve_mirror.py <directory> <keys> <port file>
"""
import functools
import http.server
import os
import ssl
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *arguments):
        pass


def main(root, keys, port):
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=root))
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(os.path.join(keys, "server.pem"), os.path.join(keys, "server.key"))
    server.socket = context.wrap_socket(server.socket, server_side=True)
    with open(port + ".part", "w") as written:
        written.write(str(server.server_address[1]))
    os.replace(port + ".part", port)
    server.serve_forever()


if __name__ == "__main__":
    main(*sys.argv[1:4])
