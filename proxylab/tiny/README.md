# :cyclone: Tiny Web server

Dave O'Hallaron (Carnegie Mellon University)

## Introduction
This is the home directory for the Tiny server, a 200-line Web server that we use in "15-213: Intro to Computer Systems" at Carnegie Mellon University.  Tiny uses the GET method to serve static content (text, HTML, GIF, and JPG files) out of ./ and to serve dynamic content by running CGI programs out of ./cgi-bin. The default page is home.html (rather than index.html) so that we can view the contents of the directory from a browser.

Tiny is neither secure nor complete, but it gives students an idea of how a real Web server works. Use for instructional purposes only.

The code compiles and runs cleanly using gcc 2.95.3 on a Linux 2.2.20 kernel.

## Usage

1. Run tiny on a server

    ```shell
    # Run "tiny <port>" on the server machine 
    ./tiny 8000
    ```

2. Point your browser at Tiny
   * static content: `http://<host>:8000`
   * dynamic content: `http://<host>:8000/cgi-bin/adder?1&2`

## Files

* `tiny.c`: The Tiny server
* `Makefile`: Makefile for tiny.c
* `home.html`: Test HTML page
* `godzilla.gif`: Image embedded in home.html	
* `cgi-bin/adder.c`: CGI program that adds two numbers
* `cgi-bin/Makefile`: Makefile for adder.c
