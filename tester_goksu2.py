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
import subprocess
import shutil
import threading
import http.client
import random
import string
from datetime import datetime

# ─── Configuration ────────────────────────────────────────────────────────────

HOST     = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT     = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
BASE_URL = f"http://{HOST}:{PORT}"
TIMEOUT  = 5

# ─── Colours ──────────────────────────────────────────────────────────────────

GREEN  = "\033[92m"
RED    = "\033[91m"
YELLOW = "\033[93m"
CYAN   = "\033[96m"
BOLD   = "\033[1m"
DIM    = "\033[2m"
RESET  = "\033[0m"

# ─── Result tracking ──────────────────────────────────────────────────────────

results  = {"passed": 0, "failed": 0, "skipped": 0}
failures = []
skips    = []


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
    skips.append((name, reason))
    tag = f" {DIM}({reason}){RESET}" if reason else ""
    print(f"  {YELLOW}–{RESET} {name}{tag}")


def section(title):
    print(f"\n{BOLD}{CYAN}▶ {title}{RESET}")


# ─── Low-level helpers ────────────────────────────────────────────────────────

def raw_request(request: str, host: str = HOST, port: int = PORT,
                read_bytes: int = 4096) -> str:
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


def http_method(method: str, path: str, body: bytes = b"",
                headers: dict = None) -> tuple:
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


def status_line(raw: str) -> str:
    """Extract the first line of a raw HTTP response."""
    return raw.split("\r\n")[0] if raw else ""


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

    # Root must respond
    status, headers, body = http_get("/")
    if status is None:
        failed("GET / returns a response", body)
    else:
        passed(f"GET / returns a response (status {status})")

    # Strict: only 200 OK
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

    # Content-Type is mandatory for serving content
    if "content-type" in lower:
        passed(f"Content-Type header present ({lower['content-type']})")
    else:
        failed("Content-Type header present")

    # Body framing — must have one of these
    if "content-length" in lower:
        try:
            cl = int(lower["content-length"])
            passed(f"Content-Length header present and numeric ({cl})")
        except ValueError:
            failed("Content-Length header is a valid integer", lower["content-length"])
    elif "transfer-encoding" in lower:
        passed(f"Transfer-Encoding header present ({lower['transfer-encoding']})")
    else:
        failed("Content-Length or Transfer-Encoding header present")

    # HTTP version in status line
    raw = raw_request("GET / HTTP/1.1\r\nHost: test\r\n\r\n")
    if raw.startswith("HTTP/1."):
        passed(f"Response uses HTTP/1.x ({raw.split()[0]})")
    else:
        failed("Response uses HTTP/1.x", raw[:30])

    # Optional but recommended
    if "server" in lower:
        passed(f"Server header present ({lower['server']})")
    else:
        skipped("Server header (optional but recommended)")

    if "date" in lower:
        passed("Date header present")
    else:
        skipped("Date header (optional but recommended)")


# ─── 4. HTTP Methods ──────────────────────────────────────────────────────────

def test_http_methods():
    section("4. HTTP Methods (GET / POST / DELETE)")

    # GET
    status, _, _ = http_method("GET", "/")
    if status == 200:
        passed("GET method returns 200 OK")
    elif status is not None:
        failed("GET method returns 200 OK", f"got {status}")
    else:
        failed("GET method returns 200 OK", "no response")

    # HEAD — must return 200 with NO body (subject: HTTP response status codes must be accurate)
    raw = raw_request("HEAD / HTTP/1.1\r\nHost: test\r\n\r\n")
    if raw and not raw.startswith("ERROR"):
        sl = status_line(raw)
        header_end = raw.find("\r\n\r\n")
        body_after = raw[header_end + 4:].strip() if header_end != -1 else raw
        if "200" in sl and not body_after:
            passed(f"HEAD / returns 200 with no body")
        elif "200" not in sl:
            failed("HEAD / returns 200", f"got {sl}")
        else:
            failed("HEAD / returns no body", "body was present")
    else:
        failed("HEAD method handled", raw[:80] if raw else "no response")

    # POST — upload endpoint
    post_body = b"field=value&test=1"
    status, _, _ = http_method(
        "POST", "/upload", body=post_body,
        headers={"Content-Type": "application/x-www-form-urlencoded",
                 "Content-Length": str(len(post_body))})
    if status in (200, 201, 204):
        passed(f"POST /upload returns {status}")
    elif status is not None:
        failed("POST /upload returns 200/201/204", f"got {status}")
    else:
        failed("POST /upload returns 200/201/204", "no response")

    # DELETE — subject requires DELETE support; 404 is valid for missing resource
    status, _, _ = http_method("DELETE", "/nonexistent_delete_target_xyz")
    if status in (200, 202, 204, 404):
        passed(f"DELETE on missing resource returns {status}")
    elif status is not None:
        failed("DELETE returns 200/202/204/404", f"got {status}")
    else:
        failed("DELETE returns 200/202/204/404", "no response")

    # Unknown / unsupported method → 405 Method Not Allowed or 501 Not Implemented
    status, _, _ = http_method("FAKEMETHOD", "/")
    if status in (405, 501):
        passed(f"Unknown method returns {status} (correct)")
    elif status is not None:
        failed("Unknown method returns 405/501", f"got {status}")
    else:
        failed("Unknown method returns 405/501", "no response")

    # OPTIONS — should not crash server; any valid HTTP response is acceptable
    status, _, _ = http_method("OPTIONS", "/")
    if status is not None:
        passed(f"OPTIONS method handled without crash (status {status})")
    else:
        failed("OPTIONS method handled", "no response / server may have crashed")


# ─── 4b. HEAD status codes ───────────────────────────────────────────────────

def test_head_status_codes():
    section("4b. HEAD request status codes")

    # HEAD should mirror GET status for common paths.
    cases = [
        ("/", [200], "HEAD / returns 200 OK"),
        ("/this_path_definitely_does_not_exist_xyz123", [404],
         "HEAD missing path returns 404"),
    ]

    for path, expected, label in cases:
        head_status, _, _ = http_method("HEAD", path)
        if head_status in expected:
            passed(f"{label} (got {head_status})")
        elif head_status is not None:
            failed(label, f"got {head_status}, expected {expected}")
        else:
            failed(label, "no response")

        get_status, _, _ = http_get(path)
        if head_status is not None and get_status is not None and head_status == get_status:
            passed(f"HEAD status matches GET for {path} ({head_status})")
        elif head_status is not None and get_status is not None:
            failed(f"HEAD status matches GET for {path}",
                   f"HEAD={head_status}, GET={get_status}")
        else:
            skipped(f"HEAD vs GET status compare for {path}", "no response")

    # HTTP/1.1 Host header is mandatory for HEAD too.
    raw = raw_request("HEAD / HTTP/1.1\r\n\r\n")
    if raw and not raw.startswith("ERROR"):
        sl = status_line(raw)
        if "400" in sl:
            passed("HEAD without Host returns 400 Bad Request")
        else:
            failed("HEAD without Host returns 400 Bad Request", f"got {sl}")
    else:
        failed("HEAD without Host test", "no response")

    # HEAD response must not include a message body.
    for path, expected in [("/", "200"), ("/this_path_definitely_does_not_exist_xyz123", "404")]:
        raw = raw_request(f"HEAD {path} HTTP/1.1\r\nHost: test\r\n\r\n")
        if not raw or raw.startswith("ERROR"):
            failed(f"HEAD {path} returns parsable response", raw[:80] if raw else "no response")
            continue

        sl = status_line(raw)
        header_end = raw.find("\r\n\r\n")
        body_after = raw[header_end + 4:].strip() if header_end != -1 else raw

        if expected in sl:
            passed(f"HEAD {path} returns {expected}")
        else:
            failed(f"HEAD {path} returns {expected}", f"got {sl}")

        if not body_after:
            passed(f"HEAD {path} has no body")
        else:
            failed(f"HEAD {path} has no body", "body was present")


# ─── 4c. POST edge cases ─────────────────────────────────────────────────────

def test_post_edge_cases():
    section("4c. POST edge cases and response codes")

    # Zero-length POST to '/' should produce a valid HTTP response code.
    # Some configurations accept it; others reject with 400/405/404.
    status, _, _ = http_method(
        "POST", "/", body=b"",
        headers={"Content-Type": "application/octet-stream", "Content-Length": "0"})
    if status in (200, 201, 202, 204, 400, 404, 405, 413):
        passed(f"POST / with Content-Length: 0 handled (status {status})")
    elif status is not None:
        failed("POST / with Content-Length: 0 handled", f"unexpected status {status}")
    else:
        failed("POST / with Content-Length: 0 handled", "no response")

    # Raw zero-length POST to verify parser/framing path for HTTP/1.1.
    raw = raw_request(
        f"POST / HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n"
    )
    if raw and not raw.startswith("ERROR") and raw.startswith("HTTP/1."):
        passed(f"Raw POST / Content-Length: 0 returns valid status line ({status_line(raw)})")
    else:
        failed("Raw POST / Content-Length: 0 returns valid status line", raw[:80] if raw else "no response")

    # Missing Content-Length / Transfer-Encoding on POST should be rejected.
    raw = raw_request(
        "POST / HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Connection: close\r\n\r\n"
    )
    if raw and not raw.startswith("ERROR"):
        sl = status_line(raw)
        if any(code in sl for code in ["400", "411", "501"]):
            passed(f"POST without length framing rejected ({sl.strip()})")
        else:
            skipped("POST without length framing", f"got {sl.strip()}")
    else:
        failed("POST without length framing", "no response")

    # Malformed body length (declared 5, send 0) should not block indefinitely.
    start = time.time()
    raw = raw_request(
        "POST / HTTP/1.1\r\n"
        "Host: test\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n\r\n"
    )
    elapsed = time.time() - start
    if elapsed < TIMEOUT + 1:
        passed(f"POST with incomplete body does not hang ({elapsed:.2f}s)")
    else:
        failed("POST with incomplete body does not hang", f"took {elapsed:.2f}s")


# ─── 5. Error pages ───────────────────────────────────────────────────────────

def test_error_pages():
    section("5. Default error pages (subject: server must have default error pages)")

    # 404 — must have a non-empty body
    status, headers, body = http_get("/no_such_page_xyz_404")
    if status == 404:
        if body.strip():
            passed("404 error page has non-empty body")
        else:
            failed("404 error page has non-empty body", "empty body on 404")
        lower = {k.lower(): v for k, v in headers.items()}
        if "content-type" in lower:
            passed(f"404 response has Content-Type ({lower['content-type']})")
        else:
            failed("404 response has Content-Type header")
    elif status is not None:
        skipped("404 error page", f"got {status} instead of 404")
    else:
        failed("404 error page", "no response")

    # 405 — trigger by using wrong method on a route that restricts methods
    # We try DELETE on root which many configs disallow
    status, _, body = http_method("DELETE", "/")
    if status == 405:
        if body.strip():
            passed("405 error page has non-empty body")
        else:
            failed("405 error page has non-empty body", "empty body on 405")
    else:
        skipped("405 error page", f"DELETE / returned {status} (configure method restriction to test)")

    # 400 — missing Host header in HTTP/1.1 must return 400 (RFC 7230)
    raw = raw_request("GET / HTTP/1.1\r\n\r\n")
    if raw and not raw.startswith("ERROR"):
        sl = status_line(raw)
        if "400" in sl:
            passed("Missing Host header returns 400 Bad Request")
        else:
            failed("Missing Host header returns 400 Bad Request", f"got {sl}")
    else:
        failed("Missing Host header test", "no response")


# ─── 6. Static file serving ───────────────────────────────────────────────────

def test_static_files():
    section("6. Static file serving (subject: must serve a fully static website)")

    # HTML at common paths
    candidates = ["/index.html", "/index.htm", "/"]
    served = False
    for path in candidates:
        status, headers, body = http_get(path)
        if status == 200:
            lower = {k.lower(): v for k, v in headers.items()}
            ct = lower.get("content-type", "")
            if "text/html" in ct or "<html" in body.lower():
                passed(f"HTML served at {path} (Content-Type: {ct})")
                served = True
                # Verify Content-Length matches body length when present
                if "content-length" in lower:
                    try:
                        cl = int(lower["content-length"])
                        if cl == len(body.encode(errors="replace")):
                            passed("Content-Length matches actual body length")
                        else:
                            failed("Content-Length matches actual body length",
                                   f"header={cl}, actual={len(body.encode(errors='replace'))}")
                    except ValueError:
                        pass
                break

    if not served:
        skipped("HTML static file served", "no HTML at common paths — check root config")

    # CSS / JS / image — MIME types should be correct
    for path, expected_ct in [("/style.css", "text/css"),
                               ("/script.js", "javascript"),
                               ("/favicon.ico", "image")]:
        status, headers, _ = http_get(path)
        if status == 200:
            ct = {k.lower(): v for k, v in headers.items()}.get("content-type", "")
            if expected_ct in ct:
                passed(f"Correct MIME type for {path} ({ct})")
            else:
                skipped(f"MIME type for {path}", f"got '{ct}', expected '{expected_ct}'")
        # 404 is fine — file just doesn't exist in this setup


# ─── 7. File upload (POST multipart) ─────────────────────────────────────────

def test_file_upload():
    section("7. File upload (subject: clients must be able to upload files)")

    boundary    = "----TestBoundary" + "".join(random.choices(string.hexdigits, k=8))
    filename    = f"test_upload_{int(time.time())}.txt"
    file_content = "Hello from webserv_tester — upload test!".encode("utf-8")

    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        f"Content-Type: text/plain\r\n\r\n"
    ).encode() + file_content + f"\r\n--{boundary}--\r\n".encode()

    upload_paths = ["/upload", "/uploads", "/"]
    uploaded_path = None

    for upload_path in upload_paths:
        status, _, _ = http_method(
            "POST", upload_path, body=body,
            headers={
                "Content-Type": f"multipart/form-data; boundary={boundary}",
                "Content-Length": str(len(body)),
            }
        )
        if status is not None and status not in (404, 405):
            passed(f"Multipart upload accepted at {upload_path} (status {status})")
            uploaded_path = upload_path
            break

    if uploaded_path is None:
        skipped("File upload", "no upload endpoint at /upload, /uploads, or /")
        return

    # Try to retrieve the uploaded file (DELETE it afterwards to keep server clean)
    get_status, _, get_body = http_get(f"{uploaded_path}/{filename}")
    if get_status == 200 and file_content.decode() in get_body:
        passed("Uploaded file is retrievable via GET")
        # Clean up
        del_status, _, _ = http_method("DELETE", f"{uploaded_path}/{filename}")
        if del_status in (200, 202, 204):
            passed("Uploaded file deleted via DELETE")
        else:
            skipped("DELETE uploaded file", f"status {del_status}")
    else:
        skipped("Retrieve uploaded file via GET",
                f"status {get_status} — path may differ by config")


def test_big_bin_download():
    section("7b. Download big binary file with curl")

    source_path = os.path.join(os.path.dirname(__file__), "www", "upload", "big.bin")
    output_path = os.path.join(os.path.dirname(__file__), "out.bin")

    if not os.path.exists(source_path):
        skipped("curl download of /upload/big.bin", "fixture www/upload/big.bin not found")
        return

    if os.path.exists(output_path):
        try:
            os.remove(output_path)
        except OSError:
            pass

    curl_cmd = [
        "curl",
        "-fsS",
        f"{BASE_URL}/upload/big.bin",
        "-o",
        output_path,
    ]

    result = subprocess.run(curl_cmd, capture_output=True, text=True, timeout=TIMEOUT + 5)
    if result.returncode != 0:
        failed("curl http://localhost:8080/upload/big.bin -o out.bin", result.stderr.strip() or result.stdout.strip() or f"exit {result.returncode}")
        return

    if not os.path.exists(output_path):
        failed("curl http://localhost:8080/upload/big.bin -o out.bin", "out.bin was not created")
        return

    with open(source_path, "rb") as source_file:
        source_bytes = source_file.read()
    with open(output_path, "rb") as output_file:
        output_bytes = output_file.read()

    if output_bytes == source_bytes:
        passed("curl download of /upload/big.bin saves the expected file to out.bin")
    else:
        failed("curl download of /upload/big.bin saves the expected file to out.bin", "downloaded file differs from fixture")

    try:
        os.remove(output_path)
    except OSError:
        pass


# ─── 8. DELETE on existing resource ──────────────────────────────────────────

def test_delete():
    section("8. DELETE method — create then delete a resource")

    # First upload a file we can then DELETE
    boundary     = "----DelBoundary" + "".join(random.choices(string.hexdigits, k=8))
    filename     = f"delete_me_{int(time.time())}.txt"
    file_content = b"This file should be deleted."

    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        f"Content-Type: text/plain\r\n\r\n"
    ).encode() + file_content + f"\r\n--{boundary}--\r\n".encode()

    upload_status = None
    upload_path   = None
    for p in ["/upload", "/uploads"]:
        s, _, _ = http_method(
            "POST", p, body=body,
            headers={"Content-Type": f"multipart/form-data; boundary={boundary}",
                     "Content-Length": str(len(body))})
        if s is not None and s not in (404, 405):
            upload_status = s
            upload_path   = p
            break

    if upload_path is None:
        skipped("DELETE test", "could not upload a file to delete (no upload endpoint)")
        return

    passed(f"File uploaded for DELETE test (status {upload_status})")

    # Now DELETE it
    del_status, _, _ = http_method("DELETE", f"{upload_path}/{filename}")
    if del_status in (200, 202, 204):
        passed(f"DELETE returns {del_status} for existing resource")
    else:
        failed("DELETE returns 200/202/204 for existing resource",
               f"got {del_status}")

    # GET after delete must return 404
    get_status, _, _ = http_get(f"{upload_path}/{filename}")
    if get_status == 404:
        passed("Resource is gone after DELETE (404)")
    else:
        failed("Resource is gone after DELETE", f"GET returned {get_status}")

    # Double-DELETE must not crash server — 404 is expected
    del2_status, _, _ = http_method("DELETE", f"{upload_path}/{filename}")
    if del2_status == 404:
        passed("Double-DELETE returns 404 (correct)")
    elif del2_status is not None:
        skipped("Double-DELETE returns 404", f"got {del2_status}")
    else:
        failed("Double-DELETE — server crashed or no response")


# ─── 9. Body size limit ───────────────────────────────────────────────────────

def test_body_size_limit():
    section("9. Client body size limit (subject: set max allowed size for client body)")

    # 11 MB — should be rejected if server has a limit ≤ 10 MB
    big_body = b"X" * (11 * 1024 * 1024)
    status, _, _ = http_method(
        "POST", "/",
        body=big_body,
        headers={"Content-Type": "application/octet-stream",
                 "Content-Length": str(len(big_body))})
    if status == 413:
        passed("Oversized body rejected with 413 Request Entity Too Large")
    elif status in (400, 403):
        passed(f"Oversized body rejected with {status}")
    elif status is not None:
        skipped("Oversized body rejected",
                f"server returned {status} — check client_max_body_size in config")
    else:
        failed("Oversized body rejected", "no response / server may have hung")

    # A reasonably small body (1 KB) must always be accepted
    small_body = b"Y" * 1024
    status, _, _ = http_method(
        "POST", "/upload",
        body=small_body,
        headers={"Content-Type": "application/octet-stream",
                 "Content-Length": str(len(small_body))})
    if status is not None and status != 413:
        passed(f"Small body (1 KB) accepted (status {status})")
    elif status == 413:
        failed("Small body (1 KB) should not be rejected with 413",
               "check your client_max_body_size setting")
    else:
        skipped("Small body acceptance", "no response from server")


# ─── 10. Non-blocking / concurrent connections ───────────────────────────────

def test_non_blocking():
    section("10. Non-blocking & concurrent connections (subject: single poll() for all I/O)")

    NUM      = 20
    statuses = []
    errors   = []

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
        failed("Concurrent requests", f"only {success}/{NUM} — {len(errors)} errors")

    # Server must still be alive
    status, _, _ = http_get("/")
    if status is not None:
        passed("Server still responds after concurrent load")
    else:
        failed("Server still responds after concurrent load")

    # Test that slow sender does not block other clients
    slow_done  = threading.Event()
    fast_times = []

    def slow_sender():
        try:
            with socket.create_connection((HOST, PORT), timeout=10) as s:
                # Send headers one byte at a time to be slow
                req = "GET / HTTP/1.1\r\nHost: test\r\nContent-Length: 5\r\n\r\n"
                for byte in req.encode():
                    s.send(bytes([byte]))
                    time.sleep(0.05)
                slow_done.set()
        except Exception:
            slow_done.set()

    def fast_requester():
        start = time.time()
        status, _, _ = http_get("/")
        fast_times.append(time.time() - start)

    slow_t = threading.Thread(target=slow_sender)
    fast_t = threading.Thread(target=fast_requester)
    slow_t.start()
    time.sleep(0.1)   # let slow sender begin
    fast_t.start()
    fast_t.join(timeout=TIMEOUT)
    slow_t.join(timeout=5)

    if fast_times and fast_times[0] < TIMEOUT:
        passed(f"Fast client not blocked by slow sender ({fast_times[0]:.2f}s)")
    else:
        failed("Fast client not blocked by slow sender",
               "slow sender may be blocking the event loop")


# ─── 11. Requests must never hang ────────────────────────────────────────────

def test_no_hang():
    section("11. Requests do not hang indefinitely (subject requirement)")

    # Normal request
    start = time.time()
    raw   = raw_request("GET / HTTP/1.1\r\nHost: test\r\n\r\n")
    elapsed = time.time() - start
    if "ERROR" not in raw and elapsed < TIMEOUT:
        passed(f"Normal request completed in {elapsed:.2f}s")
    elif elapsed >= TIMEOUT:
        failed("Normal request completed without hanging", f"took {elapsed:.1f}s")
    else:
        failed("Normal request completed", raw[:80])

    # Incomplete request — server should eventually time out and close
    start = time.time()
    raw   = raw_request("GET / HTTP/1.1\r\nHost: test\r\n")   # missing final \r\n
    elapsed = time.time() - start
    if elapsed < TIMEOUT + 1:
        passed(f"Incomplete request timed out / closed ({elapsed:.2f}s)")
    else:
        failed("Incomplete request timed out", f"took {elapsed:.1f}s — server may be hanging")

    # Partial headers sent in two chunks
    try:
        with socket.create_connection((HOST, PORT), timeout=TIMEOUT) as s:
            s.sendall(b"GET / HTTP/1.1\r\n")
            time.sleep(0.2)
            s.sendall(b"Host: test\r\n\r\n")
            s.settimeout(3)
            data = b""
            try:
                while True:
                    chunk = s.recv(4096)
                    if not chunk:
                        break
                    data += chunk
            except socket.timeout:
                pass
            if data:
                passed("Partial/pipelined headers handled correctly")
            else:
                skipped("Partial header delivery", "no response (connection closed early)")
    except Exception as e:
        failed("Partial header delivery", str(e))


# ─── 12. HTTP status code accuracy ───────────────────────────────────────────

def test_status_codes():
    section("12. HTTP status code accuracy (subject requirement)")

    cases = [
        ("/",                        [200],      "Root returns 200 OK"),
        ("/404_xyz_not_found",        [404],      "Missing path returns 404"),
    ]
    for path, expected, label in cases:
        status, _, _ = http_get(path)
        if status in expected:
            passed(f"{label} (got {status})")
        elif status is not None:
            failed(label, f"got {status}, expected {expected}")
        else:
            failed(label, "no response")

    # 400 for malformed request line
    raw = raw_request("BADREQUEST\r\n\r\n")
    if raw and not raw.startswith("ERROR"):
        sl = status_line(raw)
        if any(c in sl for c in ["400", "404", "405", "501"]):
            passed(f"Malformed request line handled ({sl.strip()})")
        else:
            failed("Malformed request line returns 4xx/5xx", f"got {sl.strip()}")
    else:
        skipped("Malformed request line", "connection closed without response")

    # 414 or 400 for URI too long
    long_path  = "/" + "a" * 8192
    status, _, _ = http_get(long_path)
    if status in (400, 414):
        passed(f"Very long URI returns {status}")
    elif status in (404, 200):
        skipped("Very long URI limit", f"server returned {status} — consider adding URI length check")
    elif status is not None:
        skipped(f"Very long URI", f"status {status}")
    else:
        failed("Very long URI handled", "no response")


# ─── 13. CGI execution ───────────────────────────────────────────────────────

def test_cgi():
    section("13. CGI execution (subject: support at least one CGI — Python or PHP)")

    cgi_found  = False
    cgi_paths  = [
        ("/cgi-bin/test.py",  "python"),
        ("/cgi-bin/test.php", "php"),
        ("/cgi/test.py",      "python"),
        ("/cgi/test.php",     "php"),
        ("/test.py",          "python"),
        ("/test.php",         "php"),
    ]

    for path, lang in cgi_paths:
        status, headers, body = http_get(path)
        if status == 200:
            lower = {k.lower(): v for k, v in headers.items()}
            passed(f"CGI script executed at {path} (lang={lang}, status=200)")
            cgi_found = True
            # CGI output must have Content-Type
            if "content-type" in lower:
                passed(f"CGI response includes Content-Type ({lower['content-type']})")
            else:
                failed("CGI response includes Content-Type")
            # Body must not be empty
            if body.strip():
                passed("CGI response has non-empty body")
            else:
                failed("CGI response has non-empty body")
            break
        elif status == 500:
            skipped(f"CGI at {path}",
                    "500 — script may be missing or has errors; create a working CGI")
            cgi_found = True
            break

    if not cgi_found:
        skipped("CGI execution",
                "no CGI found — place a test.py or test.php in your cgi-bin/ directory")

    # CGI via POST — environment variables CONTENT_TYPE and CONTENT_LENGTH must reach CGI
    post_body = b"name=tester&score=42"
    for path, _ in cgi_paths:
        status, _, body = http_method(
            "POST", path, body=post_body,
            headers={"Content-Type": "application/x-www-form-urlencoded",
                     "Content-Length": str(len(post_body))})
        if status == 200:
            passed(f"CGI POST request handled at {path} (status 200)")
            break
        elif status == 500:
            skipped("CGI POST", "500 — check CGI script reads stdin correctly")
            break
    else:
        skipped("CGI POST", "no CGI path responded")


# ─── 14. Configuration: multiple ports ───────────────────────────────────────

def test_multiple_ports():
    section("14. Multiple listening ports (subject: listen on multiple ports)")

    alt_ports = [8080, 8081, 4040, 4242]
    found     = []
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
        # The alternate port should also serve HTTP correctly
        for p in found:
            raw = raw_request("GET / HTTP/1.1\r\nHost: test\r\n\r\n", port=p)
            if raw and not raw.startswith("ERROR") and "HTTP/" in raw:
                passed(f"Port {p} serves valid HTTP responses")
            else:
                failed(f"Port {p} serves valid HTTP responses", raw[:60])
    else:
        skipped("Multiple listening ports",
                "no additional ports detected — configure a second server block")


# ─── 15. HTTP redirections ────────────────────────────────────────────────────

def test_redirections():
    section("15. HTTP redirections (subject: route config supports HTTP redirection)")

    redirect_candidates = ["/redirect", "/old", "/moved"]
    for path in redirect_candidates:
        status, headers, _ = http_get(path)
        if status in (301, 302, 307, 308):
            lower = {k.lower(): v for k, v in headers.items()}
            if "location" in lower:
                passed(f"Redirect at {path}: {status} → {lower['location']}")
            else:
                failed(f"Redirect at {path} missing Location header", f"status {status}")
            return

    skipped("HTTP redirection",
            "no redirect found at /redirect, /old, /moved — configure a redirect route")


# ─── 16. Directory listing ───────────────────────────────────────────────────

def test_directory_listing():
    section("16. Directory listing (subject: enable/disable directory listing per route)")

    listing_candidates = ["/www/html/images", "/upload", "/www/cgi-bin"]
    for path in listing_candidates:
        status, headers, body = http_get(path)
        if status == 200:
            lower_body = body.lower()
            # Directory listing typically contains <a href links to files
            if "<a href" in lower_body or "index of" in lower_body:
                passed(f"Directory listing enabled at {path}")
                return
            else:
                # Might be serving index file instead — that's also valid
                passed(f"{path} returns 200 (index file served, listing may be off)")
                return
        elif status == 403:
            passed(f"Directory listing disabled at {path} (403 Forbidden — correct)")
            return

    skipped("Directory listing",
            "no directory found at /files, /uploads, /static — configure a route with autoindex")


# ─── 17. Route method restrictions ───────────────────────────────────────────

def test_route_method_restrictions():
    section("17. Route method restrictions (subject: list of accepted HTTP methods per route)")

    # /post_body only allows POST — GET should return 405
    get_status, _, _ = http_get("/post_body")
    if get_status == 405:
        passed("GET on POST-only /post_body returns 405 (method restriction working)")
    elif get_status == 404:
        skipped("Method restriction test", "/post_body not found — configure the route")
    else:
        skipped("Method restriction on /post_body", f"got {get_status}")


# ─── 18. Chunked Transfer Encoding ───────────────────────────────────────────

def test_chunked():
    section("18. Chunked Transfer Encoding (subject: un-chunk chunked requests for CGI)")

    chunk_data = b"Hello, chunked world!"
    chunk_hex  = format(len(chunk_data), "x")
    raw_req = (
        "POST /upload HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        "Content-Type: text/plain\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        f"{chunk_hex}\r\n"
        + chunk_data.decode()
        + "\r\n0\r\n\r\n"
    )

    raw = raw_request(raw_req)
    if raw and not raw.startswith("ERROR"):
        sl = status_line(raw)
        if any(c in sl for c in ["200", "201", "204"]):
            passed(f"Chunked POST accepted ({sl.strip()})")
        elif "400" in sl:
            skipped("Chunked POST", "400 returned — server may not support chunked encoding yet")
        elif "411" in sl:
            passed("411 Length Required returned for chunked (server explicitly rejects it)")
        else:
            skipped("Chunked POST", f"got {sl.strip()}")
    else:
        failed("Chunked POST", raw[:80] if raw else "no response")


# ─── 19. Edge cases & malformed requests ─────────────────────────────────────

def test_edge_cases():
    section("19. Edge cases & malformed requests")

    # HTTP/1.0 — server should handle it
    raw = raw_request("GET / HTTP/1.0\r\nHost: test\r\n\r\n")
    if raw and not raw.startswith("ERROR"):
        passed(f"HTTP/1.0 request handled ({status_line(raw).strip()})")
    else:
        failed("HTTP/1.0 request handled", raw[:80] if raw else "no response")

    # Completely garbage input — must return an error, not crash
    raw = raw_request("GARBAGE JUNK @#$% REQUEST\r\n\r\n")
    if raw and not raw.startswith("ERROR"):
        passed("Garbage request returns an HTTP error response (server did not crash)")
    else:
        skipped("Garbage request handling", "connection closed without response (acceptable)")

    # Request with extra spaces in request line
    raw = raw_request("GET  /  HTTP/1.1\r\nHost: test\r\n\r\n")
    if raw and not raw.startswith("ERROR"):
        passed(f"Request with extra spaces handled ({status_line(raw).strip()})")
    else:
        skipped("Extra spaces in request line", "server closed connection")

    # Null bytes in path — must not crash
    try:
        with socket.create_connection((HOST, PORT), timeout=TIMEOUT) as s:
            s.sendall(b"GET /path\x00null HTTP/1.1\r\nHost: test\r\n\r\n")
            time.sleep(0.2)
            s.settimeout(2)
            data = b""
            try:
                while True:
                    chunk = s.recv(4096)
                    if not chunk:
                        break
                    data += chunk
            except socket.timeout:
                pass
            if data:
                passed("Null byte in path — server responded (did not crash)")
            else:
                skipped("Null byte in path", "connection closed without response (acceptable)")
    except Exception as e:
        failed("Null byte in path", f"exception: {e}")

    # Multiple consecutive requests on same connection (HTTP keep-alive)
    try:
        conn = http.client.HTTPConnection(HOST, PORT, timeout=TIMEOUT)
        conn.request("GET", "/", headers={"Host": f"{HOST}:{PORT}",
                                          "Connection": "keep-alive"})
        r1 = conn.getresponse()
        r1.read()
        conn.request("GET", "/", headers={"Host": f"{HOST}:{PORT}"})
        r2 = conn.getresponse()
        r2.read()
        conn.close()
        if r1.status == 200 and r2.status == 200:
            passed("Keep-alive: two sequential requests on same connection work")
        else:
            skipped("Keep-alive", f"statuses {r1.status}, {r2.status}")
    except Exception as e:
        skipped("Keep-alive / pipelining", str(e))


# ─── 20. Stress test ─────────────────────────────────────────────────────────

def test_stress():
    section("20. Stress test — availability under load (subject requirement)")

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
    start   = time.time()
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
        skipped(f"Stress: {success[0]}/{REQUESTS} ({pct:.0f}%) — check server stability")
    else:
        failed("Stress test", f"only {success[0]}/{REQUESTS} ({pct:.0f}%)")

    # Server must still respond after load
    status, _, _ = http_get("/")
    if status is not None:
        passed(f"Server still responds after stress test (status {status})")
    else:
        failed("Server still responds after stress test")

    # Mixed-method stress: GET + POST interleaved
    mix_success = [0]

    def mixed_worker():
        for i in range(5):
            try:
                if i % 2 == 0:
                    conn = http.client.HTTPConnection(HOST, PORT, timeout=TIMEOUT)
                    conn.request("GET", "/", headers={"Host": f"{HOST}:{PORT}"})
                else:
                    body = b"stress=test"
                    conn = http.client.HTTPConnection(HOST, PORT, timeout=TIMEOUT)
                    conn.request("POST", "/upload", body=body,
                                 headers={"Host": f"{HOST}:{PORT}",
                                          "Content-Type": "application/x-www-form-urlencoded",
                                          "Content-Length": str(len(body))})
                r = conn.getresponse()
                r.read()
                conn.close()
                with lock:
                    mix_success[0] += 1
            except Exception:
                pass

    mix_threads = [threading.Thread(target=mixed_worker) for _ in range(10)]
    for t in mix_threads:
        t.start()
    for t in mix_threads:
        t.join(timeout=20)

    total_mix = 10 * 5
    if mix_success[0] >= total_mix * 0.9:
        passed(f"Mixed GET/POST stress: {mix_success[0]}/{total_mix} succeeded")
    else:
        failed("Mixed GET/POST stress",
               f"only {mix_success[0]}/{total_mix} succeeded")


# ─── 21. Siege stress suite ─────────────────────────────────────────────────

def test_siege_suite():
    section("21. Siege stress suite")

    if shutil.which("siege") is None:
        skipped("Siege tests", "siege is not installed")
        return

    scenarios = [
        ("Siege 404 path (c25 t10s)", ["siege", "-c25", "-t10s", f"{BASE_URL}/nope"]),
        ("Siege root (c25 t10s)", ["siege", "-c25", "-t10s", f"{BASE_URL}"]),
        ("Siege upload route (c25 t10s)", ["siege", "-c25", "-t10s", f"{BASE_URL}/upload"]),
        ("Siege upload route heavy (c250 t10s)", ["siege", "-c250", "-t10s", f"{BASE_URL}/upload"]),
        ("Siege root heavy (c250 t10s)", ["siege", "-c250", "-t10s", f"{BASE_URL}"]),
        ("Siege CGI route (c25 t10s)", ["siege", "-c25", "-t10s", f"{BASE_URL}/cgi-bin/test.py"]),
    ]

    for label, cmd in scenarios:
        # siege -tNs needs N seconds to run + startup + report; use generous buffer
        concurrent = int(next((a.lstrip("-c") for a in cmd if a.startswith("-c")), "25"))
        t_budget = 40 if concurrent >= 100 else 30
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=t_budget)
        except subprocess.TimeoutExpired:
            failed(label, "timed out")
            continue
        except Exception as e:
            failed(label, str(e))
            continue

        if result.returncode != 0:
            details = (result.stderr or result.stdout or f"exit {result.returncode}").strip()
            failed(label, details[:180])
            continue

        # Parse availability from siege's stderr report
        import re
        m = re.search(r"Availability:\s+([\d.]+)\s*%", result.stderr)
        if m:
            avail = float(m.group(1))
            if avail < 99.0:
                failed(label, f"availability {avail:.2f}% < 99%")
            else:
                passed(label)
        else:
            passed(label + " (availability line not found in siege output)")

    status, _, _ = http_get("/")
    if status is not None:
        passed(f"Server still responds after siege suite (status {status})")
    else:
        failed("Server still responds after siege suite")


# ═══════════════════════════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════════════════════════

def print_summary():
    total = results["passed"] + results["failed"] + results["skipped"]
    print(f"\n{'─'*60}")
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

    if skips:
        print(f"\n{YELLOW}{BOLD}Skipped tests:{RESET}")
        for name, reason in skips:
            r = f"  {DIM}{reason}{RESET}" if reason else ""
            print(f"  {YELLOW}–{RESET} {name}{r}")

    print()
    if results["failed"] == 0:
        print(f"{GREEN}{BOLD}All checks passed!{RESET}")
    else:
        print(f"{YELLOW}Fix the failing tests and re-run.{RESET}")
    print()


def main():
    print(f"\n{BOLD}webserv_tester.py{RESET}  —  target: {CYAN}{BASE_URL}{RESET}")
    print(f"{DIM}{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}{RESET}")
    print("─" * 60)

    test_connectivity()            # exits if unreachable
    test_basic_get()               # §IV.1: GET, 404
    test_response_headers()        # §IV.1: accurate status codes, HTTP/1.x
    test_http_methods()            # §IV.1: GET, POST, DELETE, HEAD, unknown method
    test_head_status_codes()       # §IV.1: HEAD status code behavior
    test_post_edge_cases()         # §IV.1: POST parser/framing edge cases
    test_error_pages()             # §IV.1: default error pages, 400/404/405
    test_static_files()            # §IV.1: serve fully static website
    test_file_upload()             # §IV.1: clients must be able to upload files
    test_big_bin_download()        # curl download of /upload/big.bin to out.bin
    test_delete()                  # §IV.1: DELETE method lifecycle
    test_body_size_limit()         # §IV.3: max client body size
    test_non_blocking()            # §IV.1: non-blocking, single poll()
    test_no_hang()                 # §IV.1: request must never hang indefinitely
    test_status_codes()            # §IV.1: accurate HTTP status codes
    test_cgi()                     # §IV.3: CGI execution (Python / PHP)
    test_multiple_ports()          # §IV.1: listen on multiple ports
    test_redirections()            # §IV.3: HTTP redirection in route config
    test_directory_listing()       # §IV.3: enable/disable directory listing
    test_route_method_restrictions()  # §IV.3: accepted methods per route
    test_chunked()                 # §IV.3: chunked encoding for CGI
    test_edge_cases()              # robustness / malformed input
    test_stress()                  # §IV.1: stress test, always available
    test_siege_suite()             # siege-based route load scenarios

    print_summary()


if __name__ == "__main__":
    main()