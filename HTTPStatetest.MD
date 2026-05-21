Checks:
1. parseRequest(): HTTP does not require POST, PUT, or PATCH to have a non-empty body.
This is actually valid:
POST /upload HTTP/1.1
Host: example.com
Content-Length: 0

However, if there is NO Host, then it is invalid and a 400.


