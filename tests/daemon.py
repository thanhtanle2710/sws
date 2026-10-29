#!/usr/bin/env python3
"""Kiểm tra daemon và xử lý đồng thời trên Linux."""

import os
import pathlib
import signal
import socket
import subprocess
import tempfile
import time


PROJECT = pathlib.Path(__file__).resolve().parents[1]


def daemon_pid(arguments):
    expected = [str(PROJECT / "sws"), *arguments]
    for entry in pathlib.Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            command = (entry / "cmdline").read_bytes().rstrip(b"\0").split(b"\0")
            if [part.decode() for part in command] == expected:
                return int(entry.name)
        except (OSError, UnicodeDecodeError):
            continue
    return None


def main():
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        web = root / "www"
        web.mkdir()
        (web / "index.html").write_text("READY", encoding="utf-8")
        log = root / "requests.log"
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        arguments = ["-i", "127.0.0.1", "-p", str(port), "-l", str(log), str(web)]
        launcher = subprocess.Popen(
            [str(PROJECT / "sws"), *arguments],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        stdout, stderr = launcher.communicate(timeout=2)
        assert launcher.returncode == 0, stderr
        assert stdout == b"", stdout
        pid = None
        try:
            for _ in range(100):
                pid = daemon_pid(arguments)
                if pid is not None:
                    break
                time.sleep(0.02)
            assert pid is not None, "daemon process not found"
            with socket.create_connection(("127.0.0.1", port), timeout=2) as slow:
                slow.sendall(b"GET / HTTP/1.0\r\n")
                started = time.monotonic()
                with socket.create_connection(("127.0.0.1", port), timeout=2) as fast:
                    fast.sendall(b"GET / HTTP/1.0\r\n\r\n")
                    received = fast.recv(4096)
                assert b"HTTP/1.0 200 OK" in received, received
                assert time.monotonic() - started < 1.5
            for _ in range(50):
                if log.exists() and '"GET / HTTP/1.0" 200 5' in log.read_text():
                    break
                time.sleep(0.02)
            else:
                raise AssertionError("request log missing")
            print("daemon and concurrent connection checks passed")
        finally:
            if pid is not None:
                os.kill(pid, signal.SIGTERM)


if __name__ == "__main__":
    main()
