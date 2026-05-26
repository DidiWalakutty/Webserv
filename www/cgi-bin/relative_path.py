#!/usr/bin/python3
# Reads data.txt using a relative path.
# This verifies the server runs the CGI script with its own directory as cwd.

try:
    with open("data.txt", "r") as f:
        content = f.read().strip()
    print("Content-Type: text/plain")
    print()
    print(content)
except FileNotFoundError as e:
    print("Content-Type: text/plain")
    print()
    print(f"ERROR: could not open data.txt — wrong cwd? ({e})")
