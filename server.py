"""
Thread Factory — Lightweight Python Runner
Allows starting the compiled C backend and opening the browser with a single command.
Usage:
    python server.py
"""

import os
import sys
import subprocess
import webbrowser
import time

PORT = 8080
URL = f"http://localhost:{PORT}/"
EXE_NAME = "factory_server.exe" if sys.platform == "win32" else "factory_server"

def main():
    print("=" * 55)
    print("   🏭 Thread Factory — OS Concurrency Simulator")
    print("=" * 55)

    # Check if executable exists, if not build it
    if not os.path.exists(EXE_NAME):
        print(f"[*] Executable '{EXE_NAME}' not found. Compiling with GCC...")
        srcs = [
            os.path.join("backend", "main.c"),
            os.path.join("backend", "queue.c"),
            os.path.join("backend", "worker_pool.c"),
            os.path.join("backend", "http_server.c"),
        ]
        libs = ["-lpthread", "-lws2_32"] if sys.platform == "win32" else ["-lpthread"]
        cmd = ["gcc"] + srcs + ["-Ibackend"] + libs + ["-O2", "-o", EXE_NAME]
        
        try:
            subprocess.run(cmd, check=True)
            print("[+] Successfully compiled factory_server!")
        except Exception as e:
            print(f"[!] Compilation failed: {e}")
            print("[!] Please run build.bat or ensure gcc is in PATH.")
            sys.exit(1)

    print(f"[*] Starting C pthread server on port {PORT}...")
    server_proc = subprocess.Popen([os.path.join(".", EXE_NAME), str(PORT)])

    # Allow server to bind socket
    time.sleep(0.5)

    print(f"[*] Opening browser to {URL}")
    webbrowser.open(URL)

    try:
        server_proc.wait()
    except KeyboardInterrupt:
        print("\n[*] Terminating C backend server...")
        server_proc.terminate()
        server_proc.wait()
        print("[+] Thread Factory exited cleanly.")

if __name__ == "__main__":
    main()
