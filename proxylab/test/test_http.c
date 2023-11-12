#include "http.h"
#include <stdio.h>

char *const HttpReqeustLines[] = {
  "GET /szjy-boot/sso/codeLogin?userType=1&code=oauth_code_151b9c46ed1c3a2f92a5467305131b54&employeeNo=3122151052 HTTP/1.1\r\n",
  "Host: ipahw.xjtu.edu.cn\r\n",
  "Connection: keep-alive\r\n",
  "User-Agent: Mozilla/5.0 (Linux; Android 13; V2183A Build/TP1A.220624.014; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/110.0.5481.153 Mobile Safari/537.36 toon/2122423239 toonType/150 toonVersion/6.3.0 toongine/1.0.12 toongineBuild/12 platform/android language/zh skin/white fontIndex/0\r\n",
  "content-type: application/x-www-form-urlencoded\r\n",
  "Accept: */*\r\n",
  "X-Requested-With: synjones.commerce.xjtu\r\n",
  "Sec-Fetch-Site: same-origin\r\n",
  "Sec-Fetch-Mode: cors\r\n",
  "Sec-Fetch-Dest: empty\r\n",
  "Referer: https://ipahw.xjtu.edu.cn/sso/callback?userType=1&code=oauth_code_151b9c46ed1c3a2f92a5467305131b54&employeeNo=3122151052&state=2222&ticket=b8279e01-d450-4f77-8b3c-0e74ab646a74\r\n",
  "Accept-Encoding: gzip, deflate, br\r\n",
  "Accept-Language: zh-CN,zh;q=0.9,en-US;q=0.8,en;q=0.7\r\n",
  "Cookie: JSESSIONID=e6c36112-d614-4b45-98f8-70a66511988e\r\n",
  "\r\n"
};

int main() {
  struct HttpRequest http_request;
  int retval = InitHttpRequest(&http_request);
  if (retval != 0) {
    printf("Error: %s\n", ErrorCodeToMsg(retval));
    return 1;
  }

  // Parse request lines
  int index = 0;
  while (!IsRequestLineParsed(&http_request) || !IsHostParsed(&http_request)) {
    char *line = HttpReqeustLines[index];
    retval = ParseHttpRequest(&http_request, line);
    if (retval != 0) {
      printf("Error: %s\n", ErrorCodeToMsg(retval));
      break;
    }
    index++;
  }

  // Show parse results
  if (IsRequestLineParsed(&http_request) && IsHostParsed(&http_request)) {
    printf("Method: %s\n", http_request.request_line.method);
    printf("Url: %s\n", http_request.request_line.url);
    printf("Version: %s\n", http_request.request_line.version);
    printf("Host: %s\n", http_request.request_headers.host);
  }

  FreeHttpRequest(&http_request);
  return 0;
}