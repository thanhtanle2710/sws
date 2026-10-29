#!/usr/bin/env python3
"""Kiểm tra tích hợp các chức năng chính trên Ubuntu."""

import pathlib
import socket
import subprocess
import tempfile
import time


PROJECT = pathlib.Path(__file__).resolve().parents[1]


def response(port, request, split_at=None):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as connection:
        if split_at is None:
            connection.sendall(request)
        else:
            connection.sendall(request[:split_at])
            time.sleep(0.02)
            connection.sendall(request[split_at:])
        connection.shutdown(socket.SHUT_WR)
        chunks = []
        while True:
            chunk = connection.recv(4096)
            if not chunk:
                return b"".join(chunks)
            chunks.append(chunk)


def check(received, status, body=None):
    assert received.startswith(status), received
    assert b"\r\nDate: " in received, received
    assert b"\r\nServer: sws/0.1\r\n" in received, received
    if body is not None:
        assert body in received.partition(b"\r\n\r\n")[2], received


def main():
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        web = root / "www"
        web.mkdir()
        (web / "index.html").write_text("HOME", encoding="utf-8")
        (web / "unusual.bin").write_text("plain text", encoding="utf-8")
        listing = web / "listing"
        listing.mkdir()
        (listing / "z.txt").write_text("z", encoding="utf-8")
        (listing / "a.txt").write_text("a", encoding="utf-8")
        (listing / ".hidden").write_text("hidden", encoding="utf-8")
        scripts = root / "scripts"
        scripts.mkdir()
        script = scripts / "echo"
        script.write_text(
            '#!/bin/sh\nprintf "Content-Type: text/plain\\n\\n%s|%s|%s" '
            '"$REQUEST_METHOD" "$QUERY_STRING" "$PATH_INFO"\n',
            encoding="utf-8",
        )
        script.chmod(0o755)
        redirect = scripts / "redirect"
        redirect.write_text('#!/bin/sh\nprintf "Location: /\\n\\n"\n', encoding="utf-8")
        redirect.chmod(0o755)
        created = scripts / "created"
        created.write_text(
            '#!/bin/sh\nprintf "Status: 201 Created\\nContent-Type: text/plain\\n\\nNEW"\n',
            encoding="utf-8",
        )
        created.chmod(0o755)
        arguments = scripts / "arguments"
        arguments.write_text(
            '#!/bin/sh\nprintf "Content-Type: text/plain\\n\\n%s|%s" "$1" "$2"\n',
            encoding="utf-8",
        )
        arguments.chmod(0o755)

        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]

        server = subprocess.Popen(
            [str(PROJECT / "sws"), "-d", "-p", str(port), "-c", str(scripts), str(web)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            for _ in range(50):
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                        break
                except OSError:
                    time.sleep(0.02)
            else:
                raise AssertionError("server did not start")

            get = response(port, b"GET / HTTP/1.0\r\n\r\n", split_at=8)
            check(get, b"HTTP/1.0 200 OK", b"HOME")
            assert b"\r\nContent-Length: 4\r\n" in get, get
            check(response(port, b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"),
                  b"HTTP/1.0 200 OK", b"HOME")
            head = response(port, b"HEAD / HTTP/1.0\r\n\r\n")
            check(head, b"HTTP/1.0 200 OK")
            assert head.partition(b"\r\n\r\n")[2] == b"", head
            modified = get.split(b"Last-Modified: ", 1)[1].split(b"\r\n", 1)[0]
            conditional = response(port, b"GET / HTTP/1.0\r\nIf-Modified-Since: "
                                   + modified + b"\r\n\r\n")
            check(conditional, b"HTTP/1.0 304 Not Modified")
            directory = response(port, b"GET /listing/ HTTP/1.0\r\n\r\n")
            check(directory, b"HTTP/1.0 200 OK", b"a.txt")
            assert directory.index(b"a.txt") < directory.index(b"z.txt")
            assert b".hidden" not in directory
            check(response(port, b"GET /cgi-bin/echo/part?x=1 HTTP/1.0\r\n\r\n"),
                  b"HTTP/1.0 200 OK", b"GET|x=1|/part")
            check(response(port, b"GET /cgi-bin/redirect HTTP/1.0\r\n\r\n"),
                  b"HTTP/1.0 200 OK", b"HOME")
            check(response(port, b"GET /cgi-bin/created HTTP/1.0\r\n\r\n"),
                  b"HTTP/1.0 201 Created", b"NEW")
            check(response(port, b"GET /cgi-bin/arguments?one+two HTTP/1.0\r\n\r\n"),
                  b"HTTP/1.0 200 OK", b"one|two")
            unusual = response(port, b"GET /unusual.bin HTTP/1.0\r\n\r\n")
            check(unusual, b"HTTP/1.0 200 OK", b"plain text")
            assert b"Content-Type: text/plain" in unusual, unusual
            check(response(port, b"GET /missing HTTP/1.0\r\n\r\n"),
                  b"HTTP/1.0 404 Not Found", b"404 Not Found")
            check(response(port, b"bad request\r\n\r\n"),
                  b"HTTP/1.0 400 Bad Request", b"400 Bad Request")
            check(response(port, b"GET /../main.c HTTP/1.0\r\n\r\n"),
                  b"HTTP/1.0 403 Forbidden", b"403 Forbidden")
            print("13 smoke cases passed")
        finally:
            server.terminate()
            stdout, stderr = server.communicate(timeout=2)
            assert b'"GET / HTTP/1.0" 200 4' in stdout, stdout
            assert stderr == b"", stderr


if __name__ == "__main__":
    main()
