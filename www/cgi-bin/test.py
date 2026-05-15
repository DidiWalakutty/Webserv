#!/usr/bin/python3
import os, sys

method = os.environ.get("REQUEST_METHOD", "GET")
query  = os.environ.get("QUERY_STRING", "")

# For POST, read body from stdin; for GET, use QUERY_STRING
if method == "POST":
    length = int(os.environ.get("CONTENT_LENGTH", 0) or 0)
    raw = sys.stdin.read(length) if length > 0 else ""
else:
    raw = query

# Parse key=value&key=value
params = {}
for pair in raw.split("&"):
    if "=" in pair:
        k, v = pair.split("=", 1)
        params[k] = v

name = params.get("name", "stranger")
age  = params.get("age", "")
age_str = f", age {age}" if age else ""

print("Content-Type: text/html")
print()
print(f"""<html>
<head><title>Python CGI</title></head>
<body>
<h1>Hello, {name}{age_str}!</h1>
</body>
</html>""")

