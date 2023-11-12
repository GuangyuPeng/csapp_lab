#ifndef HTTP_H_
#define HTTP_H_

#include "csapp.h"

/**
 * Macros of error code
 */
#define SUCCESS 0
#define ERROR_MEM 1
#define ERROR_LINE_TOO_LONG 2
#define ERROR_LINE_ZERO 3
#define ERROR_READLINE_TOO_MUCH 4
#define ERROR_REQUEST_LINE_INCOMPLETE 5
#define ERROR_METHOD_TOO_LONG 6
#define ERROR_URL_TOO_LONG 7
#define ERROR_VERSION_TOO_LONG 8
#define ERROR_REQUEST_HEADER_INCOMPLETE 9
#define ERROR_HOST_TOO_LONG 10

#define METHOD_LEN 32       // max length of 'method' field in http
#define URL_LEN 2560        // max length of 'url' field in http
#define VER_LEN 32          // max length of 'version' field in http
#define HOST_LEN 256        // max length of 'Host' field in http

/**
 * The initial number of origin_lines when a HttpRequest is created
 */
#define INIT_PARSE_LINES 8
/**
 * The max number of origin_lines in HttpRequest 
 */
#define MAX_PARSE_LINES 32

/**
 * A line of data read by unix IO
 */
struct ReadLine {
  char line[MAXBUF];
  int line_finish;        // 1 if line is ended with \n else 0
};

/**
 * Meta data of a http request, which is needed by a proxy server
 */
struct HttpRequest {
  struct {
    char method[METHOD_LEN];
    char url[URL_LEN];
    char version[VER_LEN];
    /// @brief The url field in HTTP PROXY is like 'http://127.0.0.1:8080/',
    /// the proxy should remove the preceding protocol and hostname to get
    /// the actual url '/', which is stored in proxy_url.
    char *proxy_url;
  } request_line;

  struct {
    char host[HOST_LEN];
  } request_headers;

  struct ReadLine *origin_lines;
  int line_num;
  int cur_line;

  enum {
    PARSE_LINE,
    PARSE_HEADERS,
    PARSE_DATA
  } parse_state;
};

/**
 * Init a HttpRequest, all HttpRequest variables must be
 * initialized before using them, otherwise their contents
 * will be undefined and undefined mistakes will occur.
 * 
 * \returns 0 if success, otherwise an error_code which can
 * be converted to a message by function ErrorCodeToMsg.
 */
int InitHttpRequest(struct HttpRequest *http_req);

/**
 * Free a HttpRequest, any HttpRequest variable must be
 * freed after it is no longer accessed.
 */
void FreeHttpRequest(struct HttpRequest *http_req);

/**
 * Parse a line of http request, update struct HttpRequest.
 *
 * \returns 0 if success, otherwise an error_code which can
 * be converted to a message by function ErrorCodeToMsg.
 */
int ParseHttpRequest(struct HttpRequest *http_req, char *line);

/**
 * \returns 1 if the fields in http_req->request_line are all
 * parsed, otherwise 0.
 */
int IsRequestLineParsed(struct HttpRequest *http_req);

/**
 * \returns 1 if the host field in http_req->request_headers is
 * parsed, otherwise 0.
 */
int IsHostParsed(struct HttpRequest *http_req);

/**
 * Convert an error_code to a message.
 */
const char *ErrorCodeToMsg(int error_code);

#endif /* HTTP_H_ */