#include "cache.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

static const char CACHE_DIR_DEFAULT[] = ".cache/";
static char CACHE_DIR[PATH_MAX] = ".cache/";
static const char TEMP_DIR_DEFAULT[] = ".tmp/";
static char TEMP_DIR[PATH_MAX] = ".tmp/";

/**
 * \returns 1 if dir exists, 0 otherwise.
 */
int DirExist(const char *dir) {
  struct stat st_buf;
  if (stat(dir, &st_buf) == 0 && S_ISDIR(st_buf.st_mode))
    return 1;
  else
    return 0;
}

/**
 * \returns 0 if success, 1 otherwise.
 */
int CreateDir(const char *path) {
  int retval;
  char *curpath = strdup(path);
  if (!curpath) return 1;
  
  char *ptr = curpath;
  while (*ptr) {
    if (ptr != curpath && *ptr == '/') {
      *ptr = '\0';
      if (!DirExist(curpath)) {
        retval = mkdir(curpath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if (retval != 0) {
          free(curpath);
          return 1;
        }
      }
      *ptr = '/';
    }
    ptr++;
  }
  if (!DirExist(curpath)) {
    retval = mkdir(curpath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (retval != 0) {
      free(curpath);
      return 1;
    }
  }
  
  free(curpath);
  return 0;
}

/**
 * \returns 0 if success, 1 otherwise.
 */
int RemoveDir(const char *dir) {
  int retval = 0;
  struct stat st_buf;
  DIR *dir_stream = NULL;
  struct dirent *dep = NULL;
  char subdir[PATH_MAX];
  int dir_len = strlen(dir);

  // Dir does not exist, return success.
  if (access(dir, F_OK) != 0) return 0;

  retval = stat(dir, &st_buf);
  if (retval != 0) return 1;
  if (S_ISREG(st_buf.st_mode)) {
    return remove(dir);
  }
  else if (S_ISDIR(st_buf.st_mode)) {
    dir_stream = opendir(dir);
    if (!dir_stream) return 1;

    errno = 0;
    while ((dep = readdir(dir_stream)) != NULL) {
      if (strcmp(".", dep->d_name) == 0 || strcmp("..", dep->d_name) == 0)
        continue;
      if (dir_len + 1 + strlen(dep->d_name) >= sizeof(subdir)) {
        printf("RemoveDir: subdir length too long\n");
        closedir(dir_stream);
        return 1;
      }
      sprintf(subdir, "%s/%s", dir, dep->d_name);
      retval = RemoveDir(subdir);
      if (retval != 0) {
        closedir(dir_stream);
        return 1;
      }
    }
    if (errno != 0) {
      closedir(dir_stream);
      return 1;
    }

    closedir(dir_stream);
    retval = rmdir(dir);
    if (retval != 0) return 1;
  }

  return 0;
}

void InitCacheModule() {
  // Init CACHE_DIR to be "<exe_dir>/.cache/"
  // Init TEMP_DIR to be "<exe_dir>/.tmp/"
  memset(CACHE_DIR, 0, sizeof(CACHE_DIR));
  int ret = readlink("/proc/self/exe", CACHE_DIR, sizeof(CACHE_DIR)-1);
  if (ret < 0) {
    unix_error("Failed to Readlink of /proc/self/exe");
  }

  int cache_dir_default_len = strlen(CACHE_DIR_DEFAULT);
  int cache_dir_len = strlen(CACHE_DIR);
  int i;
  for (i = cache_dir_len-1; i >= 0; i--) {
    if (CACHE_DIR[i] == '/') break;
  }
  CACHE_DIR[i+1] = '\0';

  strcpy(TEMP_DIR, CACHE_DIR);
  cache_dir_len = strlen(CACHE_DIR);
  if (cache_dir_len+cache_dir_default_len >= sizeof(CACHE_DIR)) {
    app_error("Cache Module init failed: cache dir is too long");
  }
  strcat(CACHE_DIR, CACHE_DIR_DEFAULT);
  strcat(TEMP_DIR, TEMP_DIR_DEFAULT);

  // Set umask
  umask(DEF_UMASK);

  // Create CACHE_DIR
  /// delete CACHE_DIR
  printf("Remove cache dir: %s\n", CACHE_DIR);
  ret = RemoveDir(CACHE_DIR);
  if (ret != 0) {
    unix_error("Failed to remove cache dir");
  }
  /// create CACHE_DIR
  printf("Create cache dir: %s\n", CACHE_DIR);
  ret = CreateDir(CACHE_DIR);
  if (ret != 0) {
    unix_error("Failed to create cache dir");
  }

  // Create TEMP_DIR
  /// delete TEMP_DIR
  printf("Remove temp dir: %s\n", TEMP_DIR);
  ret = RemoveDir(TEMP_DIR);
  if (ret != 0) {
    unix_error("Failed to remove temp dir");
  }
  /// create CACHE_DIR
  printf("Create temp dir: %s\n", TEMP_DIR);
  ret = CreateDir(TEMP_DIR);
  if (ret != 0) {
    unix_error("Failed to create temp dir");
  }
}

int CreateCacheInfo(struct CacheInfo *cache_info,
                    const char *host, const char *url) {
  cache_info->is_open = 0;
  cache_info->is_write = 0;
  cache_info->fd = -1;
  memset(cache_info->cache_path, 0, sizeof(cache_info->cache_path));
  memset(cache_info->temp_path, 0, sizeof(cache_info->temp_path));
  memset(cache_info->error_msg, 0, sizeof(cache_info->error_msg));

  // Construct cache_path
  int cache_dir_len = strlen(CACHE_DIR);
  int host_len = strlen(host);
  int url_len = strlen(url);
  if (cache_dir_len+host_len+url_len >= sizeof(cache_info->cache_path)) {
    strcpy(cache_info->error_msg, CACHE_PATH_TOO_LONG);
    return 1;
  }

  sprintf(cache_info->cache_path, "%s%s%s", CACHE_DIR, host, url);
  int cache_path_len = strlen(cache_info->cache_path);
  while (cache_path_len > 0 &&
         cache_info->cache_path[cache_path_len-1] == '/') {
    cache_info->cache_path[cache_path_len-1] = '\0';
    cache_path_len--;
  }
  if (cache_path_len <= 0) {
    strcpy(cache_info->error_msg, CACHE_PATH_EMPTY);
    return 1;
  }

  // Construct temp_path
  int temp_dir_len = strlen(TEMP_DIR);
  if (temp_dir_len+host_len+url_len >= sizeof(cache_info->temp_path)) {
    strcpy(cache_info->error_msg, TEMP_PATH_TOO_LONG);
    return 1;
  }

  sprintf(cache_info->temp_path, "%s%s%s", TEMP_DIR, host, url);
  int temp_path_len = strlen(cache_info->temp_path);
  while (temp_path_len > 0 &&
         cache_info->temp_path[temp_path_len-1] == '/') {
    cache_info->temp_path[temp_path_len-1] = '\0';
    temp_path_len--;
  }
  if (temp_path_len <= 0) {
    strcpy(cache_info->error_msg, TEMP_PATH_EMPTY);
    return 1;
  }

  return 0;
}

void FreeCacheInfo(struct CacheInfo *cache_info) {
  // cache is opened for reading
  if (cache_info->is_open) {
    close(cache_info->fd);
    cache_info->fd = -1;
    cache_info->is_open = 0;
  }
  // cache is opened for writing
  else if (cache_info->is_write) {
    close(cache_info->fd);
    cache_info->fd = -1;
    cache_info->is_write = 0;
    if (IsCacheError(cache_info)) {
      RemoveDir(cache_info->temp_path);
    }
    else if (rename(cache_info->temp_path, cache_info->cache_path) < 0) {
      RemoveDir(cache_info->temp_path);
    }
  }
}

void RemoveCache(struct CacheInfo *cache_info) {
  // Remove cache file
  if (strlen(cache_info->cache_path) > 0) {
    RemoveDir(cache_info->cache_path);
  }
}

int IsCacheHit(struct CacheInfo *cache_info) {
  int retval = 0;
  struct stat st_buf;

  // Can't access cache file, not hit
  if (access(cache_info->cache_path, F_OK) != 0)
    return 0;

  // Can't open meta data of cache file, not hit
  retval = stat(cache_info->cache_path, &st_buf);
  if (retval != 0) {
    return 0;
  }

  // If cache_path is regular file, then hit
  return S_ISREG(st_buf.st_mode);
}

int IsCacheError(struct CacheInfo *cache_info) {
  size_t error_msg_len = strnlen(cache_info->error_msg,
                                 sizeof(cache_info->error_msg));
  return error_msg_len > 0;
}

/**
 * \returns 0 if success, 1 otherwise.
 */
int OpenCacheFile(struct CacheInfo *cache_info, int flags) {
  if (!cache_info->is_open) {
    cache_info->fd = open(cache_info->cache_path, flags, DEF_MODE);
    if (cache_info->fd < 0) {
      return 1;
    }
    rio_readinitb(&cache_info->rp, cache_info->fd);
    cache_info->is_open = 1;
  }

  return 0;
}

/**
 * \returns 0 if success, 1 otherwise.
 */
int CreateCacheDir(struct CacheInfo *cache_info) {
  int retval = 0;
  
  char *cache_dir = cache_info->cache_path;
  int cache_path_len = strlen(cache_dir);
  while (cache_path_len > 0 && cache_dir[cache_path_len-1] != '/') {
    cache_path_len--;
  }
  if (cache_path_len > 0) {
    char tmp = cache_dir[cache_path_len];
    cache_dir[cache_path_len] = '\0';
    retval = CreateDir(cache_dir);
    cache_dir[cache_path_len] = tmp;
    if (retval != 0) return 1;
  }

  return 0;
}

/**
 * \returns 0 if success, 1 otherwise.
 */
int OpenTempFile(struct CacheInfo *cache_info, int flags) {
  if (!cache_info->is_write) {
    cache_info->fd = open(cache_info->temp_path, flags, DEF_MODE);
    if (cache_info->fd < 0) {
      return 1;
    }
    rio_readinitb(&cache_info->rp, cache_info->fd);
    cache_info->is_write = 1;
  }

  return 0;
}

/**
 * \returns 0 if success, 1 otherwise.
 */
int CreateTempDir(struct CacheInfo *cache_info) {
  int retval = 0;
  
  char *temp_dir = cache_info->temp_path;
  int temp_path_len = strlen(temp_dir);
  while (temp_path_len > 0 && temp_dir[temp_path_len-1] != '/') {
    temp_path_len--;
  }
  if (temp_path_len > 0) {
    char tmp = temp_dir[temp_path_len];
    temp_dir[temp_path_len] = '\0';
    retval = CreateDir(temp_dir);
    temp_dir[temp_path_len] = tmp;
    if (retval != 0) return 1;
  }

  return 0;
}

ssize_t WriteToCache(struct CacheInfo *cache_info,
                     void *content, size_t length) {
  ssize_t retval = 0;

  if (!cache_info->is_write) {
    // Make sure cache dir is created
    retval = CreateCacheDir(cache_info);
    if (retval != 0) {
      strerror_r(errno, cache_info->error_msg, sizeof(cache_info->error_msg));
      return -1;
    }

    // Make sure temp dir is created
    retval = CreateTempDir(cache_info);
    if (retval != 0) {
      strerror_r(errno, cache_info->error_msg, sizeof(cache_info->error_msg));
      return -1;
    }

    // Make sure temp file is opened
    retval = OpenTempFile(cache_info, O_WRONLY|O_CREAT|O_EXCL);
    if (retval != 0) {
      strerror_r(errno, cache_info->error_msg, sizeof(cache_info->error_msg));
      return -1;
    }
  }

  // Write to temp file
  retval = rio_writen(cache_info->fd, content, length);
  if (retval < 0) {
    strerror_r(errno, cache_info->error_msg, sizeof(cache_info->error_msg));
    return -1;
  }

  return 0;
}

ssize_t ReadLineFromCache(struct CacheInfo *cache_info,
                          void *buf, size_t max_len) {
  ssize_t retval = 0;

  if (!cache_info->is_open) {
    // Make sure cache file is opened
    retval = OpenCacheFile(cache_info, O_RDONLY);
    if (retval != 0) {
      strerror_r(errno, cache_info->error_msg, sizeof(cache_info->error_msg));
      return -1;
    }
  }

  // Read a line from cache file
  retval = rio_readlineb(&cache_info->rp, buf, max_len);
  if (retval < 0) {
    strerror_r(errno, cache_info->error_msg, sizeof(cache_info->error_msg));
    return -1;
  }

  return retval;
}