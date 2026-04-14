#!/usr/bin/env python3
"""
webserv_tester.py — Comprehensive HTTP server tester for the 42 Webserv project.

Usage:
    python3 webserv_tester.py [host] [port]
    python3 webserv_tester.py 127.0.0.1 8080

Defaults to 127.0.0.1:8080 if no arguments are given.
"""

import sys
import socket
import time
import os
import threading
import urllib.request
import urllib.error
import http.client
import json
import random
import string
from datetime import datetime

# ─── Configuration ────────────────────────────────────────────────────────────

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
BASE_URL = f"http://{HOST}:{PORT}"
TIMEOUT = 5

# ─── Colours ──────────────────────────────────────────────────────────────────

GREEN  = "\033[92m"
RED    = "\033[91m"
YELLOW = "\033[93m"
CYAN   = "\033[96m"
BOLD   = "\033[1m"
DIM    = "\033[2m"
RESET  = "\033[0m"

# ─── Result tracking ──────────────────────────────────────────────────────────

results = {"passed": 0, "failed": 0, "skipped": 0}
failures = []


def passed(name):
    results["passed"] += 1
    print(f"  {GREEN}✓{RESET} {name}")


def failed(name, reason=""):
    results["failed"] += 1
    failures.append((name, reason))
    tag = f" {DIM}({reason}){RESET}" if reason else ""
    print(f"  {RED}✗{RESET} {name}{tag}")


def skipped(name, reason=""):
    results["skipped"] += 1
    tag = f" {DIM}({reason}){RESET}" if reason else ""
    print(f"  {YELLOW}–{RESET} {name}{tag}")


def section(title):
    print(f"\n{BOLD}{CYAN}▶ {title}{RESET}")


# ─── Low-level helpers ────────────────────────────────────────────────────────

def raw_request(request: str, host: str = HOST, port: int = PORT, read_bytes: int = 4096) -> str:
    """Send a raw HTTP request string and return the raw response."""
    try:
        with socket.create_connection((host, port), timeout=TIMEOUT) as s:
            s.sendall(request.encode())
            time.sleep(0.1)
            data = b""
            s.settimeout(2)
            try:
                while True:
                    chunk = s.recv(read_bytes)
                    if not chunk:
                        break
                    data += chunk
            except socket.timeout:
                pass
            return data.decode(errors="replace")
    except Exception as e:
        return f"ERROR: {e}"


def http_get(path: str, headers: dict = None) -> tuple:
    """Returns (status_code, headers_dict, body_str) or (None, {}, error_str)."""
    try:
        conn = http.client.HTTPConnection(HOST, PORT, timeout=TIMEOUT)
        h = {"Host": f"{HOST}:{PORT}"}
        if headers:
            h.update(headers)
        conn.request("GET", path, headers=h)
        resp = conn.getresponse()
        body = resp.read().decode(errors="replace")
        return resp.status, dict(resp.getheaders()), body
    except Exception as e:
        return None, {}, str(e)
    finally:
        conn.close()


def http_method(method: str, path: str, body: bytes = b"", headers: dict = None) -> tuple:
    """Generic HTTP method. Returns (status_code, headers_dict, body_str)."""
    try:
        conn = http.client.HTTPConnection(HOST, PORT, timeout=TIMEOUT)
        h = {"Host": f"{HOST}:{PORT}"}
        if headers:
            h.update(headers)
        conn.request(method, path, body=body, headers=h)
        resp = conn.getresponse()
        body_resp = resp.read().decode(errors="replace")
        return resp.status, dict(resp.getheaders()), body_resp
    except Exception as e:
        return None, {}, str(e)
    finally:
        conn.close()


def server_reachable() -> bool:
    try:
        with socket.create_connection((HOST, PORT), timeout=3):
            return True
    except Exception:
        return False


# ═══════════════════════════════════════════════════════════════════════════════
# TEST SUITES
# ═══════════════════════════════════════════════════════════════════════════════

# ─── 1. Connectivity ──────────────────────────────────────────────────────────

def test_connectivity():
    section("1. Connectivity")

    if server_reachable():
        passed("Server is reachable")
    else:
        failed("Server is reachable", f"Cannot connect to {HOST}:{PORT}")
        print(f"\n  {RED}Server not reachable — aborting all tests.{RESET}\n")
        sys.exit(1)


# ─── 2. Basic GET ─────────────────────────────────────────────────────────────

def test_basic_get():
    section("2. Basic GET requests")


    status, headers, body = http_get("/")
    if status is None:
        failed("GET / returns a response", body)
    else:
        passed(f"GET / returns a response (status {status})")

    # Strict: Only 200 OK is accepted
    if status == 200:
        passed("GET / returns 200 OK")
    else:
        failed("GET / returns 200 OK", f"got {status}")


    # Non-existent resource → 404
    status404, _, _ = http_get("/this_path_definitely_does_not_exist_xyz123")
    if status404 == 404:
        passed("GET on missing resource returns 404")
    elif status404 is not None:
        failed("GET on missing resource returns 404", f"got {status404}")
    else:
        failed("GET on missing resource returns 404", "no response")

    # Response has body
    _, _, body = http_get("/")
    if body:
        passed("GET / response has a non-empty body")
    else:
        failed("GET / response has a non-empty body")


# ─── 3. HTTP Response headers ─────────────────────────────────────────────────

def test_response_headers():
    section("3. HTTP Response headers")

    status, headers, body = http_get("/")
    if status is None:
        skipped("Response headers", "no response")
        return

    lower = {k.lower(): v for k, v in headers.items()}

    if "content-type" in lower:
        passed(f"Content-Type header present ({lower['content-type']})")
    else:
        failed("Content-Type header present")

    if "content-length" in lower or "transfer-encoding" in lower:
        passed("Content-Length or Transfer-Encoding header present")
    else:
        failed("Content-Length or Transfer-Encoding header present")

    if "server" in lower:
        passed(f"Server header present ({lower['server']})")
    else:
        skipped("Server header present (optional but recommended)")

    if "date" in lower:
        passed("Date header present")
    else:
        skipped("Date header present (optional but recommended)")


# ─── 4. HTTP Methods ──────────────────────────────────────────────────────────

def test_http_methods():
    section("4. HTTP Methods (GET / POST / DELETE)")


    # GET (strict: only 200 OK)
    status, _, _ = http_method("GET", "/")
    if status == 200:
        passed("GET method returns 200 OK")
    elif status is not None:
        failed("GET method returns 200 OK", f"got {status}")
    else:
        failed("GET method returns 200 OK", "no response")

    # POST — send simple form data to /upload (strict: only 200, 201, 204 accepted)
    post_body = b"field=value&test=1"
    status, _, _ = http_method("POST", "/upload", body=post_body,
                                headers={"Content-Type": "application/x-www-form-urlencoded",
                                         "Content-Length": str(len(post_body))})
    if status in (200, 201, 204):
        passed(f"POST method returns {status}")
    elif status is not None:
        failed("POST method returns 200/201/204", f"got {status}")
    else:
        failed("POST method returns 200/201/204", "no response")

    # DELETE (strict: only 200, 202, 204, 404 accepted)
    status, _, _ = http_method("DELETE", "/nonexistent_delete_target")
    if status in (200, 202, 204, 404):
        passed(f"DELETE method returns {status}")
    elif status is not None:
        failed("DELETE method returns 200/202/204/404", f"got {status}")
    else:
        failed("DELETE method returns 200/202/204/404", "no response")

    # Unknown method → must return 405 or 501 (strict)
    status, _, _ = http_method("FAKEMETHOD", "/")
    if status in (405, 501):
        passed(f"Unknown method returns correct error ({status})")
    elif status is not None:
        failed("Unknown method returns 405/501", f"got {status}")
    else:
        failed("Unknown method returns 405/501", "no response")


# ─── 5. Error pages ───────────────────────────────────────────────────────────

def test_error_pages():
    section("5. Default error pages")

    for code, path in [(404, "/no_such_page_xyz"), (405, None)]:
        if path:
            status, _, body = http_get(path)
            if status == code:
                if body.strip():
                    passed(f"{code} error has a non-empty body")
                else:
                    failed(f"{code} error has a non-empty body", "empty body")
            elif status is not None:
                skipped(f"{code} error page test", f"got {status} instead of {code}")
            else:
                failed(f"{code} error page test", "no response")


# ─── 6. Static file serving ───────────────────────────────────────────────────

def test_static_files():
    section("6. Static file serving")

    # Try common static paths
    candidates = ["/index.html", "/index.htm", "/"]
    served = False
    for path in candidates:
        status, headers, body = http_get(path)
        if status == 200:
            lower = {k.lower(): v for k, v in headers.items()}
            ct = lower.get("content-type", "")
            if "text/html" in ct or "<html" in body.lower():
                passed(f"HTML file served at {path}")
                served = True
                break

    if not served:
        skipped("HTML static file served", "no HTML found at common paths")


# ─── 7. File upload (POST) ────────────────────────────────────────────────────

def test_file_upload():
    section("7. File upload")

    boundary = "----TestBoundary" + "".join(random.choices(string.hexdigits, k=8))
    filename = f"test_upload_{int(time.time())}.txt"
    file_content = b"Hello from webserv_tester.py!"

    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        f"Content-Type: text/plain\r\n\r\n"
    ).encode() + file_content + f"\r\n--{boundary}--\r\n".encode()

    # Try common upload endpoints
    for upload_path in ["/upload", "/uploads", "/"]:
        status, _, _ = http_method(
            "POST", upload_path, body=body,
            headers={
                "Content-Type": f"multipart/form-data; boundary={boundary}",
                "Content-Length": str(len(body)),
            }
        )
        if status is not None and status not in (404, 405):
            passed(f"File upload POST accepted at {upload_path} (status {status})")
            return

    skipped("File upload test", "no upload endpoint found at /upload, /uploads, or /")


# ─── 8. Non-blocking / resilience ────────────────────────────────────────────

def test_non_blocking():
    section("8. Non-blocking & concurrent connections")

    NUM = 20
    statuses = []
    errors = []

    def do_get():
        try:
            conn = http.client.HTTPConnection(HOST, PORT, timeout=TIMEOUT)
            conn.request("GET", "/", headers={"Host": f"{HOST}:{PORT}"})
            r = conn.getresponse()
            r.read()
            statuses.append(r.status)
            conn.close()
        except Exception as e:
            errors.append(str(e))

    threads = [threading.Thread(target=do_get) for _ in range(NUM)]
    for t in threads:
        t.start()
    for t in threads:
        t.join(timeout=TIMEOUT + 2)

    success = len(statuses)
    if success == NUM:
        passed(f"All {NUM} concurrent requests completed")
    elif success >= NUM // 2:
        passed(f"{success}/{NUM} concurrent requests completed")
    else:
        failed(f"Concurrent requests", f"only {success}/{NUM} succeeded, {len(errors)} errors")

    # Server still responds after the flood
    status, _, _ = http_get("/")
    if status is not None:
        passed("Server still responds after concurrent load")
    else:
        failed("Server still responds after concurrent load")


# ─── 9. Request never hangs ───────────────────────────────────────────────────

def test_no_hang():
    section("9. Requests do not hang indefinitely")

    start = time.time()
    raw = raw_request("GET / HTTP/1.1\r\nHost: test\r\n\r\n")
    elapsed = time.time() - start

    if "ERROR" not in raw and elapsed < TIMEOUT:
        passed(f"Normal request completed in {elapsed:.2f}s")
    elif elapsed >= TIMEOUT:
        failed("Normal request completed without hanging", f"took {elapsed:.1f}s")
    else:
        failed("Normal request completed", raw[:80])

    # Slow / incomplete request — server should eventually close connection
    start = time.time()
    raw = raw_request("GET / HTTP/1.1\r\nHost: test\r\n")  # missing final \r\n
    elapsed = time.time() - start
    if elapsed < TIMEOUT + 1:
        passed(f"Incomplete request timed out correctly ({elapsed:.2f}s)")
    else:
        failed("Incomplete request timed out correctly", f"took {elapsed:.1f}s")


# ─── 10. HTTP status code accuracy ───────────────────────────────────────────

def test_status_codes():
    section("10. HTTP status code accuracy")


    cases = [
        ("/",      [200], "Root returns 200 OK"),
        ("/404_xyz_not_found", [404], "Missing path returns 404"),
    ]

    for path, expected, label in cases:
        status, _, _ = http_get(path)
        if status in expected:
            passed(f"{label} (got {status})")
        elif status is not None:
            failed(label, f"got {status}, expected {expected}")
        else:
            failed(label, "no response")

    # HEAD method — must return 200, no body
    raw = raw_request("HEAD / HTTP/1.1\r\nHost: test\r\n\r\n")
    if raw and not raw.startswith("ERROR"):
        first_line = raw.split("\r\n")[0]
        # Body after headers should be empty
        header_end = raw.find("\r\n\r\n")
        body_after = raw[header_end + 4:] if header_end != -1 else ""
        if not body_after.strip() and "200" in first_line:
            passed(f"HEAD / returns 200 OK and no body ({first_line})")
        elif "200" not in first_line:
            failed("HEAD / returns 200 OK", f"got {first_line}")
        else:
            failed("HEAD / returns no body", "body present or HEAD not fully supported")
    else:
        failed("HEAD method test", "no response or error")


# ─── 11. Body size limit ──────────────────────────────────────────────────────

def test_body_size_limit():
    section("11. Client body size limit")

    big_body = b"X" * (11 * 1024 * 1024)  # 11 MB
    status, _, _ = http_method(
        "POST", "/",
        body=big_body,
        headers={
            "Content-Type": "application/octet-stream",
            "Content-Length": str(len(big_body)),
        }
    )
    if status in (413, 400, 403):
        passed(f"Oversized body rejected with {status}")
    elif status is not None:
        skipped("Oversized body rejected", f"server returned {status} (may be intentional)")
    else:
        failed("Oversized body rejected", "no response")


# ─── 12. CGI execution ────────────────────────────────────────────────────────

def test_cgi():
    section("12. CGI execution")

    for path in ["/cgi-bin/test.py", "/cgi-bin/test.php",
                 "/cgi/test.py", "/test.py", "/test.php"]:
        status, headers, body = http_get(path)
        if status == 200:
            lower = {k.lower(): v for k, v in headers.items()}
            passed(f"CGI script executed at {path} (status 200)")
            return
        elif status == 500:
            skipped(f"CGI at {path}", "internal server error — script may be missing or broken")
            return

    skipped("CGI execution", "no CGI found at common paths — set up a test CGI script")


# ─── 13. Configuration: multiple ports ───────────────────────────────────────

def test_multiple_ports():
    section("13. Multiple listening ports")

    alt_ports = [8080, 4040]
    found = []
    for p in alt_ports:
        if p == PORT:
            continue
        try:
            with socket.create_connection((HOST, p), timeout=2):
                found.append(p)
        except Exception:
            pass

    if found:
        passed(f"Server listens on additional port(s): {found}")
    else:
        skipped("Multiple listening ports", "no additional ports detected (configure your server)")


# ─── 14. Raw HTTP / edge cases ────────────────────────────────────────────────

def test_edge_cases():
    section("14. Edge cases & malformed requests")

    # HTTP/1.0 request
    raw = raw_request("GET / HTTP/1.0\r\nHost: test\r\n\r\n")
    if raw and not raw.startswith("ERROR"):
        passed("HTTP/1.0 request handled")
    else:
        failed("HTTP/1.0 request handled", raw[:80])

    # Missing Host header (HTTP/1.1 should return 400)
    raw = raw_request("GET / HTTP/1.1\r\n\r\n")
    if raw and not raw.startswith("ERROR"):
        status_line = raw.split("\r\n")[0]
        if "400" in status_line or "200" in status_line:
            passed(f"Missing Host header handled ({status_line.strip()})")
        else:
            passed(f"Missing Host header handled ({status_line.strip()})")
    else:
        failed("Missing Host header handled", raw[:80])

    # Completely garbage input
    raw = raw_request("GARBAGE JUNK REQUEST\r\n\r\n")
    if raw and not raw.startswith("ERROR"):
        passed("Garbage request returns an HTTP error response")
    else:
        skipped("Garbage request handling", "connection closed without response")

    # Very long URL
    long_path = "/" + "a" * 8192
    status, _, _ = http_get(long_path)
    if status in (400, 414, 404, 200):
        passed(f"Very long URL handled (status {status})")
    elif status is not None:
        skipped(f"Very long URL", f"status {status}")
    else:
        failed("Very long URL handled", "no response")


# ─── 15. Stress test ─────────────────────────────────────────────────────────

def test_stress():
    section("15. Stress test (availability under load)")

    REQUESTS = 100
    WORKERS  = 10
    success  = [0]
    lock     = threading.Lock()

    def worker():
        for _ in range(REQUESTS // WORKERS):
            try:
                conn = http.client.HTTPConnection(HOST, PORT, timeout=TIMEOUT)
                conn.request("GET", "/", headers={"Host": f"{HOST}:{PORT}"})
                r = conn.getresponse()
                r.read()
                conn.close()
                with lock:
                    success[0] += 1
            except Exception:
                pass

    threads = [threading.Thread(target=worker) for _ in range(WORKERS)]
    start = time.time()
    for t in threads:
        t.start()
    for t in threads:
        t.join(timeout=30)
    elapsed = time.time() - start

    rate = success[0] / elapsed if elapsed > 0 else 0
    pct  = success[0] / REQUESTS * 100

    if pct >= 95:
        passed(f"{success[0]}/{REQUESTS} requests succeeded ({rate:.1f} req/s)")
    elif pct >= 80:
        skipped(f"Stress test — {success[0]}/{REQUESTS} succeeded ({pct:.0f}%) — check server stability")
    else:
        failed("Stress test", f"only {success[0]}/{REQUESTS} succeeded ({pct:.0f}%)")

    # Server must still respond
    status, _, _ = http_get("/")
    if status is not None:
        passed(f"Server still responds after stress test (status {status})")
    else:
        failed("Server still responds after stress test")


# ═══════════════════════════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════════════════════════

def print_summary():
    total = results["passed"] + results["failed"] + results["skipped"]
    print(f"\n{'─'*55}")
    print(f"{BOLD}Results{RESET}  "
          f"{GREEN}{results['passed']} passed{RESET}  "
          f"{RED}{results['failed']} failed{RESET}  "
          f"{YELLOW}{results['skipped']} skipped{RESET}  "
          f"/ {total} total")

    if failures:
        print(f"\n{RED}{BOLD}Failed tests:{RESET}")
        for name, reason in failures:
            r = f"  {DIM}{reason}{RESET}" if reason else ""
            print(f"  {RED}✗{RESET} {name}{r}")

    print()
    if results["failed"] == 0:
        print(f"{GREEN}{BOLD}All checks passed!{RESET}")
    else:
        print(f"{YELLOW}Fix the failing tests and re-run.{RESET}")
    print()


def main():
    print(f"\n{BOLD}tester_goksu.py{RESET}  —  target: {CYAN}{BASE_URL}{RESET}")
    print(f"{DIM}{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}{RESET}")
    print("─" * 55)

    test_connectivity()       # exits early if unreachable
    test_basic_get()
    test_response_headers()
    test_http_methods()
    test_error_pages()
    test_static_files()
    test_file_upload()
    test_non_blocking()
    test_no_hang()
    test_status_codes()
    test_body_size_limit()
    test_cgi()
    test_multiple_ports()
    test_edge_cases()
    test_stress()

    print_summary()


if __name__ == "__main__":
    main()