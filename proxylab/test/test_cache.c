#include "cache.h"

const char *HOST1 = "ipahw.xjtu.edu.cn";
const char *URL1 = "/szjy-boot/sso/codeLogin?userType=1&code=oauth_code_"
                   "151b9c46ed1c3a2f92a5467305131b54&employeeNo=3122151052";
const char *HOST2 = "www.baidu.com";
const char *URL2 = "/";

char *CONTENT1 = "ipahw.xjtu.edu.cn\nHello, ipahw ! ! !";
char *CONTENT2 = "www.baidu.com\nHello, baidu ! ! !";

int main() {
  InitCacheModule();

  ssize_t retval = 0;
  struct CacheInfo cache_info1;
  struct CacheInfo cache_info2;
  
  retval = CreateCacheInfo(&cache_info1, HOST1, URL1);
  if (retval != 0) {
    printf("Create cache_info1 error: %s\n", cache_info1.error_msg);
    return 1;
  }
  retval = CreateCacheInfo(&cache_info2, HOST2, URL2);
  if (retval != 0) {
    printf("Create cache_info2 error: %s\n", cache_info2.error_msg);
    return 1;
  }

  printf("\n");
  printf("Cache path1: %s\n", cache_info1.cache_path);
  printf("Cache path2: %s\n", cache_info2.cache_path);

  printf("\n");
  printf("Cache path1 hit: %d\n", IsCacheHit(&cache_info1));
  printf("Cache path2 hit: %d\n", IsCacheHit(&cache_info2));

  printf("\n");
  printf("Writing to cache1 and cache2 ...\n");
  retval = WriteToCache(&cache_info1, CONTENT1, strlen(CONTENT1));
  if (retval != 0) {
    printf("Write cache_info1 error: %s\n", cache_info1.error_msg);
  }
  retval = WriteToCache(&cache_info2, CONTENT2, strlen(CONTENT2));
  if (retval != 0) {
    printf("Write cache_info2 error: %s\n", cache_info2.error_msg);
  }
  retval = WriteToCache(&cache_info1, CONTENT1, strlen(CONTENT1));
  if (retval != 0) {
    printf("Write cache_info1 error: %s\n", cache_info1.error_msg);
  }
  retval = WriteToCache(&cache_info2, CONTENT2, strlen(CONTENT2));
  if (retval != 0) {
    printf("Write cache_info2 error: %s\n", cache_info2.error_msg);
  }
  FreeCacheInfo(&cache_info1);
  FreeCacheInfo(&cache_info2);

  printf("\n");
  printf("Cache path1 hit: %d\n", IsCacheHit(&cache_info1));
  printf("Cache path2 hit: %d\n", IsCacheHit(&cache_info2));

  char buffer[MAXLINE];
  printf("\n");
  printf("Reading from cache1 and cache1 and cache2 ...\n");
  printf("Cache1:\n");
  while ((retval = ReadLineFromCache(&cache_info1, buffer, MAXLINE)) > 0) {
    printf("%s", buffer);
  }
  printf("\n");
  if (retval < 0) {
    printf("Read cache_info1 error: %s\n", cache_info1.error_msg);
    return 1;
  }
  
  printf("Cache2:\n");
  while ((retval = ReadLineFromCache(&cache_info2, buffer, MAXLINE)) > 0) {
    printf("%s", buffer);
  }
  printf("\n");
  if (retval < 0) {
    printf("Read cache_info2 error: %s\n", cache_info2.error_msg);
    return 1;
  }

  FreeCacheInfo(&cache_info1);
  FreeCacheInfo(&cache_info2);

  return 0;
}