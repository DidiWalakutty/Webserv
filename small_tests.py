#some other tests

import http.client

conn = http.client.HTTPConnection('localhost', 8080, timeout=5)
conn.request('POST', '/', body=b'')
# conn.request("DELETE", "/")
r1 = conn.getresponse()
b1 = r1.read()
print('Status:', r1.status, 'len:', len(b1), 'conn:', r1.getheader('Connection'))

try:
    conn.request('HEAD', '/')
    r2 = conn.getresponse()
    b2 = r2.read()
    print('HEAD status:', r2.status, 'len:', len(b2), 'conn:', r2.getheader('Connection'))
except Exception as e:
    print('HEAD exception:', repr(e))

try:
    conn.request('POST', '/', body=(
        'HTTP/1.1\r\n'
        'Host: test\r\n'
        'Content-Type: text/plain\r\n'
        'Content-Length: 5\r\n'
        'Connection: close\r\n\r\n'
    ))
    r3 = conn.getresponse()
    b3 = r3.read()
    print('POST with body status:', r3.status, 'len:', len(b3), 'conn:', r3.getheader('Connection'))
except Exception as e:
    print('POST with body exception:', repr(e))
    
conn.close()

