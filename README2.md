## USAGE:

## Check whether the server is listening on 127.0.0.1:8080

### List listening TCP ports (shows PID/program):
ss -ltnp | grep ':8080'

*or*

sudo netstat -tulnp | grep ':8080'

### List processes holding the port:
lsof -i :8080

If the server is bound to 0.0.0.0:8080 or 127.0.0.1:8080, these will show it.

## Connect / send an HTTP request from the terminal

### Simple HTTP GET with curl:
curl -v http://127.0.0.1:8080/

### Raw HTTP request using netcat (shows full round-trip):
printf "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n" | nc 127.0.0.1 8080

### Interactive test with telnet (type request manually):
telnet 127.0.0.1 8080

*then type:*

GET / HTTP/1.1

Host: 127.0.0.1

(blank line)