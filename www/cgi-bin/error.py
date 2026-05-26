#!/usr/bin/python3
# This script crashes with a runtime error before producing any output.
# The server should return 500 Internal Server Error.

result = 1 / 0  # ZeroDivisionError — no headers emitted
print("Content-Type: text/plain")
print()
print(result)
