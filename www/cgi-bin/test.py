#!/usr/bin/env python3

import os
import sys

print("Content-Type: text/html\r")
print()

body = ""

if os.environ.get("REQUEST_METHOD") == "POST":
    try:
        length = int(os.environ.get("CONTENT_LENGTH", "0"))
        body = sys.stdin.read(length)
    except:
        body = "[failed to read body]"

print(f"""
<!DOCTYPE html>
<html>
<head>
    <title>Python CGI Test</title>
    <style>
        body {{
            font-family: Arial;
            background: #202124;
            color: #e8eaed;
            padding: 20px;
        }}
        table {{
            border-collapse: collapse;
            width: 100%;
        }}
        td, th {{
            border: 1px solid #555;
            padding: 8px;
        }}
        th {{
            background: #333;
        }}
        pre {{
            background: #111;
            padding: 10px;
        }}
    </style>
</head>
<body>

<h1>Python CGI Works</h1>

<h2>Environment Variables</h2>

<table>
<tr><th>Variable</th><th>Value</th></tr>
""")

keys = [
    "GATEWAY_INTERFACE",
    "REQUEST_METHOD",
    "SCRIPT_NAME",
    "SERVER_NAME",
    "SERVER_PORT",
    "SERVER_PROTOCOL",
    "SERVER_SOFTWARE",
    "REMOTE_ADDR",
    "QUERY_STRING",
    "SCRIPT_FILENAME",
    "CONTENT_LENGTH",
    "CONTENT_TYPE",
]

for key in keys:
    print(f"<tr><td>{key}</td><td>{os.environ.get(key, '')}</td></tr>")

print("""
</table>

<h2>POST Body</h2>
<pre>
""")

print(body)

print("""
</pre>

</body>
</html>
""")

print("Python CGI stderr test", file=sys.stderr)