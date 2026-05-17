#!/usr/bin/env python3
import os
import socket
import subprocess
import sys
import time


def main():
    env = os.environ.copy()
    env.setdefault("MINI_DB_HOST", "127.0.0.1")
    env.setdefault("MINI_DB_PORT", "3306")
    env.setdefault("MINI_DB_USER", "mini_user")
    env.setdefault("MINI_DB_PASSWORD", "mini_password")
    env.setdefault("MINI_DB_NAME", "mini_server")
    env["MINI_IDLE_TIMEOUT_SECONDS"] = "1"

    proc = subprocess.Popen(
        ["./server"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        env=env,
    )
    try:
        time.sleep(1)
        sock = socket.create_connection(("127.0.0.1", 8080), timeout=3)
        try:
            time.sleep(3)
            sock.settimeout(1)
            data = sock.recv(1)
            if data != b"":
                raise AssertionError("idle connection was not closed")
        finally:
            sock.close()
    finally:
        proc.terminate()
        proc.wait(timeout=5)

    print("timeout smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
