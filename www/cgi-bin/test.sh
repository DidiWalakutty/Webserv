#!/usr/bin/bash

BODY=""

if [ "$REQUEST_METHOD" = "POST" ]; then
    read -r -n "$CONTENT_LENGTH" BODY
fi

echo "Content-Type: text/html"
echo

cat << EOF
<!DOCTYPE html>
<html>
<head>
    <title>Shell CGI Test</title>
    <style>
        body {
            font-family: Arial;
            background: #1e1e1e;
            color: #ffffff;
            padding: 20px;
        }
        table {
            border-collapse: collapse;
            width: 100%;
        }
        td, th {
            border: 1px solid #666;
            padding: 8px;
        }
        th {
            background: #333;
        }
        pre {
            background: #111;
            padding: 10px;
        }
    </style>
</head>
<body>

<h1>Shell CGI Works</h1>

<h2>Environment Variables</h2>

<table>
<tr><th>Variable</th><th>Value</th></tr>

<tr><td>GATEWAY_INTERFACE</td><td>$GATEWAY_INTERFACE</td></tr>
<tr><td>REQUEST_METHOD</td><td>$REQUEST_METHOD</td></tr>
<tr><td>SCRIPT_NAME</td><td>$SCRIPT_NAME</td></tr>
<tr><td>SERVER_NAME</td><td>$SERVER_NAME</td></tr>
<tr><td>SERVER_PORT</td><td>$SERVER_PORT</td></tr>
<tr><td>SERVER_PROTOCOL</td><td>$SERVER_PROTOCOL</td></tr>
<tr><td>SERVER_SOFTWARE</td><td>$SERVER_SOFTWARE</td></tr>
<tr><td>REMOTE_ADDR</td><td>$REMOTE_ADDR</td></tr>
<tr><td>QUERY_STRING</td><td>$QUERY_STRING</td></tr>
<tr><td>SCRIPT_FILENAME</td><td>$SCRIPT_FILENAME</td></tr>
<tr><td>CONTENT_LENGTH</td><td>$CONTENT_LENGTH</td></tr>
<tr><td>CONTENT_TYPE</td><td>$CONTENT_TYPE</td></tr>

</table>

<h2>POST Body</h2>

<pre>
$BODY
</pre>

</body>
</html>
EOF

echo "Shell CGI stderr test" >&2