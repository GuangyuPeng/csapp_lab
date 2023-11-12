#include "http.h"

#include <string.h>
#include <stdlib.h>

const char *const ErrorMsgs[] = {
  "",
  "Fail to allocate dynamic memory.",
  "Read line length exceeds max buffer size.",
  "Read line has zero length.",
  "Too much origin_lines in HttpRequest.",
  "Request line is incomplete.",
  "Method field length exceeds METHOD_LEN.",
  "Url field length exceeds URL_LEN.",
  "Version field length exceeds VER_LEN.",
  "Request header is incomplete.",
  "Host filed length exceeds HOST_LEN."
};

int InitHttpRequest(struct HttpRequest *http_req) {
  // Set all strings to empty strings
  memset(&http_req->request_line, 0, sizeof(http_req->request_line));
  memset(&http_req->request_headers, 0, sizeof(http_req->request_headers));
  http_req->origin_lines = NULL;

  // Allocate memory to origin_lines
  void *ptr = malloc(sizeof(struct ReadLine) * INIT_PARSE_LINES);
  if (!ptr) return ERROR_MEM;
  http_req->origin_lines = (struct ReadLine *)ptr;
  http_req->line_num = INIT_PARSE_LINES;
  http_req->cur_line = 0;

  // Set parse_state
  http_req->parse_state = PARSE_LINE;

  return 0;
}

void FreeHttpRequest(struct HttpRequest *http_req) {
  // Free dynamic memory
  if (http_req->origin_lines) {
    free(http_req->origin_lines);
    http_req->origin_lines = NULL;
  }
}

int AddLineToHttpRequest(struct HttpRequest *http_req, char *line) {
  // Check line length
  size_t line_length = strnlen(line, MAXBUF);
  if (line_length == MAXBUF) return ERROR_LINE_TOO_LONG;
  if (line_length == 0) return ERROR_LINE_ZERO;

  int line_finish = (line[line_length-1] == '\n');

  // Check if origin_lines has enough memory
  if (http_req->cur_line == http_req->line_num) {
    if (http_req->line_num >= MAX_PARSE_LINES)
      return ERROR_READLINE_TOO_MUCH;
    http_req->line_num *= 2;
    void *ptr = realloc(http_req->origin_lines,
                        sizeof(struct ReadLine) * http_req->line_num);
    if (!ptr) {
      free(http_req->origin_lines);
      http_req->origin_lines = NULL;
      return ERROR_MEM;
    }
    http_req->origin_lines = ptr;
  }

  // Add line
  strcpy(http_req->origin_lines[http_req->cur_line].line, line);
  http_req->origin_lines[http_req->cur_line].line_finish = line_finish;
  http_req->cur_line++;

  return 0;
}

/**
 * Extract the actual url in the http proxy protocol.
 * 
 * The url field in HTTP PROXY is like 'http://127.0.0.1:8080/',
 * the proxy should remove the preceding protocol and hostname to get
 * the actual url '/', which is stored in http_req->request_line.proxy_url.
 */
void GetProxyUrl(struct HttpRequest *http_req) {
  int forslash_cnt = 0;
  char *url = http_req->request_line.url;
  
  // Find the thrid '/' and set proxy url
  while (*url) {
    if (*url == '/') {
      forslash_cnt++;
      if (forslash_cnt == 3) {
        http_req->request_line.proxy_url = url;
        return;
      }
    }
    url++;
  }

  // By default, the proxy url is NULL
  http_req->request_line.proxy_url = NULL;
}

int ParseRequestLine(struct HttpRequest *http_req, char *line) {
  char *str = line;
  char *cur = line;
  int cnt = 0;
  
  while (1) {
    if (*cur == '\0') return ERROR_REQUEST_LINE_INCOMPLETE;
    if (*cur == ' ' || *cur == '\r' || *cur == '\n') {
      *cur = '\0';
      int str_len = strlen(str);
      if (str_len > 0) {
        if (cnt == 0) {               // method
          if (str_len >= METHOD_LEN) return ERROR_METHOD_TOO_LONG;
          strcpy(http_req->request_line.method, str);
          cnt++;
        }
        else if (cnt == 1) {          // url
          if (str_len >= URL_LEN) return ERROR_URL_TOO_LONG;
          strcpy(http_req->request_line.url, str);
          cnt++;
          GetProxyUrl(http_req);
        }
        else if (cnt == 2) {          // version
          if (str_len >= VER_LEN) return ERROR_VERSION_TOO_LONG;
          strcpy(http_req->request_line.version, str);
          cnt++;
        }
      }
      str = cur+1;
    }
    if (cnt == 3) break;
    cur++;
  }

  http_req->parse_state = PARSE_HEADERS;
  return 0;
}

int ParseHeaders(struct HttpRequest *http_req, char *line) {
  // This means the end of request headers.
  if (strcmp(line, "\r\n") == 0) {
    http_req->parse_state = PARSE_DATA;
    return 0;
  }

  // Get the field name
  char *field = line;
  char *cur = line;
  while (*cur != ':' && *cur != '\0') cur++;
  if (*cur == '\0') return ERROR_REQUEST_HEADER_INCOMPLETE;
  *cur = '\0';

  cur++;
  while (*cur == ' ') cur++;

  // Get the value
  char *value = cur;
  int value_len = strlen(value);
  if (value_len <= 2) return ERROR_REQUEST_HEADER_INCOMPLETE;
  value[value_len-2] = '\0';
  value_len = value_len-2;

  // Update HttpRequest
  if (strcmp(field, "Host") == 0) {
    if (value_len >= HOST_LEN) return ERROR_HOST_TOO_LONG;
    strcpy(http_req->request_headers.host, value);
  }

  return 0;
}

int ParseHttpRequest(struct HttpRequest *http_req, char *line) {
  // Add a new ReadLine to HttpRequest
  int retval = AddLineToHttpRequest(http_req, line);
  if (retval != 0) return retval;

  // Since a long long line may be divided into multiple 'struct ReadLine's,
  // we should combine them to a complete line before parsing it.
  struct ReadLine *read_line_end = http_req->origin_lines+http_req->cur_line-1;
  struct ReadLine *read_line_start = read_line_end-1;
  /// if the newest line is not ended with '\n', then return.
  /// else, then parse all lines that are not parsed.
  if (!read_line_end->line_finish) return 0;
  int tot_size = strlen(read_line_end->line) + 1;
  char *tot_line = NULL;
  char *cur_ptr = NULL;
  
  while (read_line_start >= http_req->origin_lines &&
         !read_line_start->line_finish) {
    tot_size += strlen(read_line_start->line);
    read_line_start -= 1;
  }

  tot_line = (char *)malloc(tot_size);
  if (!tot_line) return ERROR_MEM;
  cur_ptr = tot_line;

  while (read_line_start < read_line_end) {
    read_line_start += 1;
    int tmp_len = strlen(read_line_start->line);
    strcpy(cur_ptr, read_line_start->line);
    cur_ptr += tmp_len;
  }
  (*cur_ptr) = '\0';

  // Parse Request Line
  if (http_req->parse_state == PARSE_LINE) {
    retval = ParseRequestLine(http_req, tot_line);
    if (retval != 0) {
      free(tot_line);
      return retval;
    }
  }
  // Parse Headers
  else if (http_req->parse_state == PARSE_HEADERS) {
    retval = ParseHeaders(http_req, tot_line);
    if (retval != 0) {
      free(tot_line);
      return retval;
    }
  }

  free(tot_line);
  return 0;
}

int IsRequestLineParsed(struct HttpRequest *http_req) {
  return http_req->parse_state != PARSE_LINE;
}

int IsHostParsed(struct HttpRequest *http_req) {
  int host_len = strlen(http_req->request_headers.host);
  return host_len > 0;
}

const char *ErrorCodeToMsg(int error_code) {
  return ErrorMsgs[error_code];
}