#!/usr/bin/env python3
"""Generate a local CA/server identity and run the HTTPS verification fixture."""
import http.server
import os
from pathlib import Path
import socketserver
import ssl
import subprocess
import sys

root = Path(__file__).resolve().parent / ".tmp" / "https"
root.mkdir(parents=True, exist_ok=True)
key = root / "key.pem"
certificate = root / "certificate.pem"
config = root / "openssl.cnf"
config.write_text("""[req]
distinguished_name = subject
x509_extensions = extensions
prompt = no
[subject]
CN = localhost
[extensions]
subjectAltName = DNS:localhost
basicConstraints = critical,CA:TRUE
keyUsage = critical,digitalSignature,keyEncipherment,keyCertSign
extendedKeyUsage = serverAuth
""")
if not certificate.exists():
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
        "-keyout", str(key), "-out", str(certificate), "-config", str(config),
    ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"LuaC mbedTLS test\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *args):
        pass

class Server(socketserver.TCPServer):
    allow_reuse_address = True

with Server(("127.0.0.1", 0), Handler) as server:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certificate, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    print(server.server_address[1], flush=True)
    server.serve_forever()
