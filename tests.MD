Webserv Testing Checklist:

1. Root / (Home page)

- URL: http://localhost:8080/
- Expected: index.html contents
- Edge case: /index.html should also work
- Curl test:

curl -i http://localhost:8080/
curl -i http://localhost:8080/index.html

2. Upload /upload

- URL: http://localhost:8080/upload
- Expected: upload_index.html
- Edge case: /upload/ should behave the same
- Curl test:

curl -i http://localhost:8080/upload
curl -i http://localhost:8080/upload/

- POST test (file upload):

curl -i -X POST -F "file=@test.txt" http://localhost:8080/upload

- DELETE test: (existing uploaded file)

curl -i -X DELETE http://localhost:8080/upload/test.txt

3. Images /images

- URL: http://localhost:8080/images
- Expected: Directory listing if autoindex=true
- Single file: /images/bird.png or /images/cat.jpg
- Curl test for listing:

curl -i http://localhost:8080/images

- Curl test for image content: (outputs raw data safely)
curl http://localhost:8080/images/bird.png --output bird.png

4. CGI /cgi-bin

- URL: http://localhost:8080/cgi-bin/
- Expected: Runs CGI scripts (GET/POST)
- Edge case: /cgi-bin/script.cgi or /cgi-bin/script.cgi/
- Curl test (GET):

curl -i http://localhost:8080/cgi-bin/hello.cgi

- Curl test (POST):

curl -i -X POST -d "name=Didi" http://localhost:8080/cgi-bin/hello.cgi

5. Redirect /redirect

- URL: http://localhost:8080/redirect
- Expected: Permanent redirect (301) to http://localhost:8080/
- Curl test:

curl -i http://localhost:8080/redirect

- Optional follow redirect:

curl -i -L http://localhost:8080/redirect

6. Second server /

- URL: http://localhost:4040/
- Expected: www/second_site/index.html contents
- Edge case: /index.html works
- Curl test:

curl -i http://localhost:4040/
curl -i http://localhost:4040/index.html

- Upload allowed? / only allows GET, POST, DELETE → test POST if needed

7. Error Pages

- Test invalid file/path:
- curl -i http://localhost:8080/nonexistent
- Test invalid method:

curl -i -X PUT http://localhost:8080/

- Expected: Proper error page from www/html/errors/*.html


____________
✅ Notes:

Use --output - for binary files like images to prevent terminal gibberish.
____________