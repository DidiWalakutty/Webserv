#!/usr/bin/php-cgi

<?php

echo "Content-Type: text/html\r\n\r\n";

$body = "";

if ($_SERVER["REQUEST_METHOD"] === "POST")
{
    $body = file_get_contents("php://stdin");
}

?>

<!DOCTYPE html>
<html>
<head>
    <title>PHP CGI Test</title>
    <style>
        body {
            font-family: Arial;
            background: #202124;
            color: #e8eaed;
            padding: 20px;
        }
        table {
            border-collapse: collapse;
            width: 100%;
        }
        td, th {
            border: 1px solid #555;
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

<h1>PHP CGI Works</h1>

<h2>Environment Variables</h2>

<table>
<tr><th>Variable</th><th>Value</th></tr>

<?php

$keys = [
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
    "REDIRECT_STATUS"
];

foreach ($keys as $key)
{
    $value = getenv($key);
    echo "<tr><td>$key</td><td>$value</td></tr>";
}

?>

</table>

<h2>GET Variables</h2>

<pre>
<?php print_r($_GET); ?>
</pre>

<h2>POST Body</h2>

<pre>
<?php echo htmlspecialchars($body); ?>
</pre>

</body>
</html>

<?php
file_put_contents("php://stderr", "PHP CGI stderr test\n");
?>