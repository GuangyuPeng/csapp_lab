#ifndef CACHE_H_
#define CACHE_H_

#include "csapp.h"
#include <limits.h>

/**
 * Macros of error messages.
 */
#define CACHE_PATH_TOO_LONG "Cache path exceeds PATH_MAX"
#define CACHE_PATH_EMPTY "Cache path is emtpy"
#define TEMP_PATH_TOO_LONG "Temp path exceeds PATH_MAX"
#define TEMP_PATH_EMPTY "Temp path is emtpy"

/**
 * Meta data for the cache of a http response.
 */
struct CacheInfo {
  int is_open;                  // 1 if cache file is opened for reading
  int is_write;                 // 1 if cache file is opened for writing
  int fd;                       // opened cache/temp file description
  rio_t rp;                     // robust io buffer
  char cache_path[PATH_MAX];    // cache file path when reading from
  char temp_path[PATH_MAX];     // temp file path before writing is done
  char error_msg[MAXLINE];      // the message of last error
};

/**
 * Do some initiate work to the cache module.
 * Note: this function should be called first only once before
 * any other functions in cache module.
 */
void InitCacheModule();

/**
 * Create a CacheInfo with the host and url of a http request.
 * Note: host and url should not be NULL nor empty string.
 * 
 * \returns 0 if success, 1 otherwise. If returns 1, the error reason is
 * stored in cache_info.error_msg.
 */
int CreateCacheInfo(struct CacheInfo *cache_info,
                    const char *host, const char *url);

/**
 * Release all the resources that cache_info obtains from OS.
 * Note: this function should be called after cache_info
 * is not used anymore.
 */
void FreeCacheInfo(struct CacheInfo *cache_info);

/**
 * Remove the cache content represented by cache_info.
 */
void RemoveCache(struct CacheInfo *cache_info);

/**
 * \returns 1 if cache hit, 0 otherwise.
 */
int IsCacheHit(struct CacheInfo *cache_info);

/**
 * Check if CacheInfo has error.
 * 
 * \returns 1 if has error, 0 otherwise.
 */
int IsCacheError(struct CacheInfo *cache_info);

/**
 * Write content to cache, with the length of content.
 * 
 * \returns 0 if success, -1 otherwise. If returns -1, the error reason is
 * stored in cache_info.error_msg.
 */
ssize_t WriteToCache(struct CacheInfo *cache_info,
                     void *content, size_t length);

/**
 * Read a line from cache to buffer, with the max length of buffer.
 * 
 * \returns -1 if error, 0 if EOF, bytes read otherwise. If returns -1, 
 * the error reason is stored in cache_info.error_msg.
 */
ssize_t ReadLineFromCache(struct CacheInfo *cache_info,
                          void *buf, size_t max_len);

#endif /* CACHE_H_ */