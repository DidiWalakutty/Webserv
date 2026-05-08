#some other tests

import http.client

conn = http.client.HTTPConnection('localhost', 8080, timeout=5)
# conn.request('POST', '/', body=b'')
conn.request("DELETE", "/")
r1 = conn.getresponse()
b1 = r1.read()
print('Status:', r1.status, 'len:', len(b1), 'conn:', r1.getheader('Connection'))

# try:
#     conn.request('HEAD', '/')
#     r2 = conn.getresponse()
#     b2 = r2.read()
#     print('HEAD status:', r2.status, 'len:', len(b2), 'conn:', r2.getheader('Connection'))
# except Exception as e:
#     print('HEAD exception:', repr(e))

conn.close()

