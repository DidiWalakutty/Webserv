#!/bin/bash
# tests_CGI.sh — CGI evaluation tests
# Tests: GET/POST methods, correct cwd, error handling, timeout, server stability.

PASS=0
FAIL=0
BASE="http://localhost:8080/cgi-bin"

# Check HTTP status code in curl -sv output
check() {
	local desc="$1"
	local expected="$2"
	local actual="$3"
	if echo "$actual" | grep -q "< HTTP/1.1 $expected"; then
		echo "[PASS] $desc (got $expected)"
		((PASS++))
	else
		local got=$(echo "$actual" | grep "< HTTP/1.1" | head -1)
		echo "[FAIL] $desc (expected $expected, got: $got)"
		((FAIL++))
	fi
}

# Check that the response body contains a pattern
check_body() {
	local desc="$1"
	local pattern="$2"
	local actual="$3"
	if echo "$actual" | grep -q "$pattern"; then
		echo "[PASS] $desc"
		((PASS++))
	else
		echo "[FAIL] $desc (pattern '$pattern' not found in response)"
		((FAIL++))
	fi
}

# ─── GET tests ───────────────────────────────────────────────────────────────

echo "=== [1] GET CGI with query string ==="
RES=$(curl -sv "${BASE}/test.py?name=Alice&age=30" 2>&1)
check "GET /cgi-bin/test.py?name=Alice&age=30" "200" "$RES"
check_body "Body contains 'Alice'" "Alice" "$RES"

echo ""
echo "=== [2] GET CGI with no query string (default values) ==="
RES=$(curl -sv "${BASE}/test.py" 2>&1)
check "GET /cgi-bin/test.py (no args)" "200" "$RES"
check_body "Body contains default name 'stranger'" "stranger" "$RES"

echo ""
echo "=== [3] GET non-existent CGI script (404) ==="
RES=$(curl -sv "${BASE}/doesnotexist.py" 2>&1)
check "GET /cgi-bin/doesnotexist.py" "404" "$RES"

# ─── POST tests ──────────────────────────────────────────────────────────────

echo ""
echo "=== [4] POST CGI with form data ==="
RES=$(curl -sv -X POST "${BASE}/test.py" \
	-H "Content-Type: application/x-www-form-urlencoded" \
	-d "name=Bob&age=25" 2>&1)
check "POST /cgi-bin/test.py" "200" "$RES"
check_body "Body contains 'Bob'" "Bob" "$RES"

echo ""
echo "=== [5] POST CGI with empty body ==="
RES=$(curl -sv -X POST "${BASE}/test.py" \
	-H "Content-Type: application/x-www-form-urlencoded" \
	-d "" 2>&1)
check "POST /cgi-bin/test.py (empty body)" "200\|400" "$RES"

# ─── CGI sets its own status code ────────────────────────────────────────────

echo ""
echo "=== [6] CGI script returns non-200 status via 'Status:' header ==="
RES=$(curl -sv "${BASE}/test3.py" 2>&1)
check "CGI-set status 404 (test3.py)" "404" "$RES"

# ─── Correct working directory for relative path access ──────────────────────

echo ""
echo "=== [7] CGI relative path file access (correct cwd) ==="
RES=$(curl -sv "${BASE}/relative_path.py" 2>&1)
check "GET /cgi-bin/relative_path.py" "200" "$RES"
check_body "Body contains file content 'cgi_test_data'" "cgi_test_data" "$RES"

# ─── Error handling ──────────────────────────────────────────────────────────

echo ""
echo "=== [8] CGI script crashes before emitting headers (502) ==="
RES=$(curl -sv "${BASE}/error.py" 2>&1)
check "CGI crash before headers → 502" "502" "$RES"

echo ""
echo "=== [9] CGI script with empty/header-only output ==="
RES=$(curl -sv "${BASE}/test4.py" 2>&1)
check "CGI header-only output — no crash" "200\|500" "$RES"

# ─── Timeout / infinite loop ─────────────────────────────────────────────────

echo ""
echo "=== [10] CGI infinite loop — server must kill and return 504 ==="
RES=$(curl -sv --max-time 15 "${BASE}/infinite_loop.py" 2>&1)
check "CGI infinite loop → 504" "504" "$RES"

echo ""
echo "=== [11] CGI sleep exceeds timeout (test2.py sleeps 10 s, timeout is 5 s) ==="
RES=$(curl -sv --max-time 15 "${BASE}/test2.py" 2>&1)
check "CGI sleep 10 s → 504" "504" "$RES"

# ─── Server stability after all errors ───────────────────────────────────────

echo ""
echo "=== [12] Server still responds after all CGI errors ==="
RES=$(curl -sv http://localhost:8080/ 2>&1)
check "Server alive — GET / returns 200" "200" "$RES"

echo ""
echo "================================"
echo "Results: $PASS passed, $FAIL failed"
