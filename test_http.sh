#!/bin/bash

PASS=0
FAIL=0

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

echo "=== GET root ==="
RES=$(curl -sv http://localhost:8080/ 2>&1)
check "GET /" "200" "$RES"

echo ""
echo "=== POST upload file ==="
RES=$(curl -sv -X POST http://localhost:8080/upload -F "file=@test.txt" 2>&1)
check "POST /upload" "201" "$RES"

echo ""
echo "=== GET uploaded file ==="
RES=$(curl -sv http://localhost:8080/upload/test.txt 2>&1)
check "GET /upload/test.txt" "200" "$RES"

echo ""
echo "=== DELETE uploaded file ==="
RES=$(curl -sv -X DELETE http://localhost:8080/upload/test.txt 2>&1)
check "DELETE /upload/test.txt" "204" "$RES"

echo ""
echo "=== UNKNOWN method (should not crash) ==="
RES=$(curl -sv -X FOOBAR http://localhost:8080/ 2>&1)
check "UNKNOWN method" "405\|501\|400" "$RES"

echo ""
echo "=== GET non-existent path (404) ==="
RES=$(curl -sv http://localhost:8080/doesnotexist 2>&1)
check "GET /doesnotexist" "404" "$RES"

echo ""
echo "=== POST on GET-only route (405) ==="
RES=$(curl -sv -X POST http://localhost:8080/ -d "data=test" 2>&1)
check "POST on GET-only /" "405" "$RES"

echo ""
echo "=== POST exceeding max body size (413) ==="
RES=$(curl -sv -X POST http://localhost:8080/upload -F "file=@netpractice.pdf" 2>&1) # 20 MB of data
check "POST /upload body too large" "413" "$RES"

echo ""
echo "=== Redirect (301) ==="
RES=$(curl -sv http://localhost:8080/redirect 2>&1)
check "GET /redirect" "301" "$RES"

echo ""
echo "================================"
echo "Results: $PASS passed, $FAIL failed"
