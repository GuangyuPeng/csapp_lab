#include "csapp.h"
#include "http.h"
#include "cache.h"

#include <stdio.h>
#include <stdatomic.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#define ENABLE_STATIC_CACHE 1   // turn on/off static cache
#define NTHREAD   4             // number of working threads
#define MAX_REQ   80            // the max requests a thread can handle

#define POOL_AVAIL_WAIT_NS 10000000   // 10ms
#define SELECT_TIMEOUT_US 10000       // 10ms

#define HTTP_PORT "80"

/**
 * State of a proxy request.
 */
enum ProxyState {
  UNCONNECTED,                  // unconnected to the target server
  CONNECTED,                    // connected to the target server
  CACHED                        // requested data is cached
};

/**
 * Meta data of a proxy request.
 */
struct ProxyMeta {
  int client_fd;                // file descriptor of connection to client
  int server_fd;                // file descriptor of connection to server
  rio_t client_rp;              // robust io buffer for client_fd
  rio_t server_rp;              // robust io buffer for server_fd
  char src_host[HOST_LEN];      // host name of client
  char src_port[HOST_LEN];      // port of client
  enum ProxyState proxy_state;
  struct HttpRequest http_request;
  struct CacheInfo cache_info;
};

/**
 * A pool of requests handled by each thread.
 */
struct RequestPool {
  struct ProxyMeta requests[MAX_REQ];
  int enabled[MAX_REQ];         // If each request is effective
  int req_num;                  // Number of effective requests
  fd_set read_set;              // Set of all active file descriptors
  int max_fd;                   // Max descriptor in read_set
  pthread_mutex_t pool_mutex;   // mutex to access RequestPool members
  pthread_cond_t pool_empty;    // condition variable if pool is empty
};

/**
 * Global variables
 */
struct RequestPool request_pools[NTHREAD];

/* number of request_pools that are not full */
int avail_pools = NTHREAD;
/* mutex and condition variable to access avail_pools */
pthread_mutex_t avail_pools_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pools_avail_cond = PTHREAD_COND_INITIALIZER;
const long pools_avail_cond_wait_ns = POOL_AVAIL_WAIT_NS;

/* tid of threads */
pthread_t workers[NTHREAD];

/* listen socket file descriptor */
char *listen_port = NULL;
/* listen socket file descriptor */
int listenfd = -1;

/* global flag to exit */
volatile atomic_int exit_flag = ATOMIC_VAR_INIT(0);

/**
 * Fuctions
 */

/**
 * Init a request pool structure.
 * 
 * \param pool the request pool.
 */
void InitRequestPool(struct RequestPool *pool);

/**
 * Add a new connection to the request pool.
 * 
 * \param pool the request pool.
 * \param client_fd socket fd of client.
 * \param hostname host name of client.
 * \param port port of client.
 * 
 * \returns 1 if success, 0 otherwise.
 */
int AddRequestToPool(struct RequestPool *pool, int client_fd,
                     char *hostname, char *port);

/**
 * Find index of the next active request in requests in
 * RequestPool structure. The active request has its 
 * client_fd or server_fd set in ready_set. The function
 * starts finding from start_index.
 * 
 * \param pool the request pool.
 * \param ready_setp ptr to the ready set of active descriptors.
 * \param start_index the index to start from.
 * 
 * \returns the index of the next active request if found,
 *          -1 otherwise.
 */
int GetNextActiveRequestInPool(struct RequestPool *pool,
                               fd_set *ready_setp,
                               int start_index);

/**
 * Handle a client_fd in UNCONNECTED state in a worker thread.
 * 
 * \param request the ProxyMeta structure containing the client_fd.
 * \param worker_id the index of worker thread.
 * 
 * \returns 1 if successfully handled, but the request process is not finished;
 *          0 if successfully handled, and the request process is finished;
 *          -1 if error occurs.
 */
int HandleUnconnectedClientFd(struct RequestPool *pool,
                              struct ProxyMeta *request,
                              size_t worker_id);

/**
 * Handle a client_fd in CONNECTED state in a worker thread.
 * 
 * \param request the ProxyMeta structure containing the client_fd.
 * \param worker_id the index of worker thread.
 * 
 * \returns 1 if successfully handled, but the request process is not finished;
 *          0 if successfully handled, and the request process is finished;
 *          -1 if error occurs.
 */
int HandleConnectedClientFd(struct ProxyMeta *request, size_t worker_id);

/**
 * Handle a client_fd in Cached state in a worker thread.
 * 
 * \param request the ProxyMeta structure containing the client_fd.
 * \param worker_id the index of worker thread.
 * 
 * \returns 1 if successfully handled, but the request process is not finished;
 *          0 if successfully handled, and the request process is finished;
 *          -1 if error occurs.
 */
int HandleCachedClientFd(struct ProxyMeta *request, size_t worker_id);

/**
 * Handle a server_fd in a worker thread.
 * 
 * \param request the ProxyMeta structure containing the server_fd.
 * \param worker_id the index of worker thread.
 * 
 * \returns 1 if successfully handled, but the request process is not finished;
 *          0 if successfully handled, and the request process is finished;
 *          -1 if error occurs.
 */
int HandleServerFd(struct ProxyMeta *request, size_t worker_id);

/**
 * Remove a request in a RequestPool, free all resources of the request.
 * 
 * \param pool the request pool.
 * \param index index of the request to remove.
 */
void RmRequestInpool(struct RequestPool *pool, int index);

/**
 * Main thread function: Allocate requests to workers.
 */
void HandleConnection(int connfd, char *hostname, char *port);

/**
 * Work thread function: Serve requests.
 */
void *WorkThread(void *args);

/**
 * Set global exit flag to be true.
 */
void SetExitFlag();

/**
 * Test if global exit flag is true.
 * 
 * \returns 1 if exit flag is true, 0 otherwise.
 */
int TestExitFlag();

/**
 * Signal Handlers
 */
void ExitSignalHandler(int sig);

int main(int argc, char **argv) {
  int connfd;
  char hostname[HOST_LEN], port[HOST_LEN];
  struct sockaddr_storage clientaddr;
  socklen_t clientlen = sizeof(clientaddr);
  sigset_t mask, prev_mask;

  // Check command line args
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // Block all signals
  sigfillset(&mask);
  pthread_sigmask(SIG_BLOCK, &mask, &prev_mask);

  // Init cache module
  InitCacheModule();

  // Init request_pools
  for (ssize_t i = 0; i < NTHREAD; i++) {
    InitRequestPool(&request_pools[i]);
  }

  // Create worker threads
  for (ssize_t i = 0; i < NTHREAD; i++) {
    /// All worker threads will inherit the blocked sigmask
    /// from main thread
    Pthread_create(&workers[i], NULL, WorkThread, (void *)i);
  }

  // Install signal handlers
  Signal(SIGPIPE, SIG_IGN);
  Signal(SIGHUP, ExitSignalHandler);
  Signal(SIGQUIT, ExitSignalHandler);
  Signal(SIGINT, ExitSignalHandler);
  Signal(SIGTERM, ExitSignalHandler);

  // Start listening on port
  listen_port = argv[1];
  listenfd = Open_listenfd(listen_port);
  printf("Proxy listening on port %s ...\n", listen_port);

  // Unblock all signals
  // Afterwards, when receiving signals, ExitSignalHandler will
  // close listenfd, and set exit flag.
  pthread_sigmask(SIG_SETMASK, &prev_mask, NULL);

  // The main thread
  while (!TestExitFlag()) {
    connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
    if (connfd >= 0) {
      getnameinfo((SA *)&clientaddr, clientlen,
                  hostname, HOST_LEN,
                  port, HOST_LEN, 0);
      printf("[Main thread] Get connection from %s:%s, client_fd: %d\n",
             hostname, port, connfd);
      HandleConnection(connfd, hostname, port);
    }
  }

  // Cancel all worker threads
  for (ssize_t i = 0; i < NTHREAD; i++) {
    pthread_cancel(workers[i]);
  }

  // Reap all worker threads
  printf("Reap all worker threads ...\n");
  for (ssize_t i = 0; i < NTHREAD; i++) {
    pthread_join(workers[i], NULL);
  }

  // Free all resources
  printf("Free all resources ...\n");
  /// Destroy mutexes and condition variables
  for (ssize_t i = 0; i < NTHREAD; i++) {
    pthread_mutex_trylock(&request_pools[i].pool_mutex);
    pthread_mutex_unlock(&request_pools[i].pool_mutex);
    pthread_mutex_destroy(&request_pools[i].pool_mutex);

    pthread_cond_destroy(&request_pools[i].pool_empty);
  }
  /// Close client_fd and server_fd
  /// Free HttpRequest structures
  /// Free CacheInfo structures
  for (ssize_t i = 0; i < NTHREAD; i++) {
    struct RequestPool *pool = &request_pools[i];
    for (ssize_t j = 0; j < MAX_REQ; j++) {
      struct ProxyMeta *request = &pool->requests[j];
      if (!pool->enabled[j]) continue;

      if (request->client_fd >= 0) close(request->client_fd);
      if (request->server_fd >= 0) close(request->server_fd);
      FreeHttpRequest(&request->http_request);
      if (ENABLE_STATIC_CACHE) FreeCacheInfo(&request->cache_info);
    }
  }

  return 0;
}

/**
 * Get the time after ns ns from now.
 */
static struct timespec GetTimeoutTime(int64_t ns) {
  int64_t sec_to_ns = 1000000000;
  struct timespec start_tm;
  struct timespec end_tm;

  clock_gettime(CLOCK_REALTIME, &start_tm);
  int64_t start_tm_ns = start_tm.tv_sec * sec_to_ns + start_tm.tv_nsec;
  int64_t end_time_ns = start_tm_ns + ns;

  end_tm.tv_sec = end_time_ns / sec_to_ns;
  end_tm.tv_nsec = end_time_ns % sec_to_ns;

  return end_tm;
}

void InitRequestPool(struct RequestPool *pool) {
  memset(pool->enabled, 0, sizeof(pool->enabled));
  pool->req_num = 0;
  FD_ZERO(&pool->read_set);
  pool->max_fd = -1;
  pthread_mutex_init(&pool->pool_mutex, NULL);
  pthread_cond_init(&pool->pool_empty, NULL);
}

void HandleConnection(int connfd, char *hostname, char *port) {
  static int next_worker = 0;   // next worker thread to handle the connection
  int retval = 0;
  int worker_id = next_worker;

  // Wait for request_pools to be not full
  pthread_mutex_lock(&avail_pools_mutex);
  while (!TestExitFlag() && avail_pools <= 0) {
    /// If signals of exit arrived here, thus ExitSignalHandler is executed
    /// before calling pthread_cond_wait, then the main thread may never be
    /// waken up. So we use pthread_cond_timedwait here.
    struct timespec timeout = GetTimeoutTime(pools_avail_cond_wait_ns);
    pthread_cond_timedwait(&pools_avail_cond, &avail_pools_mutex, &timeout);
  }
  pthread_mutex_unlock(&avail_pools_mutex);

  if (TestExitFlag()) {
    close(connfd);
    return;
  }

  // Add the request to next available request pool
  for (int i = 0; i < NTHREAD; i++) {
    worker_id = (next_worker+i) % NTHREAD;
    retval = AddRequestToPool(
      &request_pools[worker_id], connfd, hostname, port);
    if (retval) {
      next_worker = (worker_id + 1) % NTHREAD;
      break;
    }
  }
}

/**
 * Returns 1 if success, 0 otherwise.
 */ 
int AddRequestToPool(struct RequestPool *pool, int client_fd,
                     char *hostname, char *port) {
  pthread_mutex_lock(&pool->pool_mutex);
  // The pool is full to add a new request
  if (pool->req_num >= MAX_REQ) {
    pthread_mutex_unlock(&pool->pool_mutex);
    return 0;
  }

  // Find an available location in pool->requests
  for (int i = 0; i < MAX_REQ; i++) {
    if (pool->enabled[i]) continue;
    
    /// Init ProxyMeta structure
    pool->requests[i].client_fd = client_fd;
    pool->requests[i].server_fd = -1;
    rio_readinitb(&pool->requests[i].client_rp, client_fd);
    int src_host_size = sizeof(pool->requests[i].src_host);
    int src_port_size = sizeof(pool->requests[i].src_port);
    strncpy(pool->requests[i].src_host, hostname, src_host_size-1);
    strncpy(pool->requests[i].src_port, port, src_port_size-1);
    pool->requests[i].src_host[src_host_size-1] = '\0';
    pool->requests[i].src_port[src_port_size-1] = '\0';
    pool->requests[i].proxy_state = UNCONNECTED;
    /// Init HttpRuquest struture in ProxyMeta structure
    int ret = InitHttpRequest(&pool->requests[i].http_request);
    if (ret != 0) {
      /// Usually, the program should never reach here ! ! !
      pthread_mutex_unlock(&pool->pool_mutex);
      printf("AddRequestToPool failed: %s\n", ErrorCodeToMsg(ret));
      exit(1);
    }

    pool->enabled[i] = 1;
    break;
  }

  // Add fd to read_set for the 'select' system call
  FD_SET(client_fd, &pool->read_set);
  // Update max_fd
  if (client_fd > pool->max_fd) pool->max_fd = client_fd;
  
  // Update req_num
  pool->req_num++;
  if (pool->req_num == 1) {
    /// Wakeup thread that is waiting because pool is empty
    pthread_cond_signal(&pool->pool_empty);
  }
  if (pool->req_num == MAX_REQ) {
    /// Decrease avail_pools by 1
    pthread_mutex_lock(&avail_pools_mutex);
    avail_pools--;
    pthread_mutex_unlock(&avail_pools_mutex);
  }

  pthread_mutex_unlock(&pool->pool_mutex);

  return 1;
}

void RmRequestInpool(struct RequestPool *pool, int index) {
  struct ProxyMeta *request = &pool->requests[index];

  // Close and free resources
  if (request->client_fd >= 0) close(request->client_fd);
  if (request->server_fd >= 0) close(request->server_fd);
  FreeHttpRequest(&request->http_request);
  if (ENABLE_STATIC_CACHE) FreeCacheInfo(&request->cache_info);

  // Update meta data of RequestPool
  pthread_mutex_lock(&pool->pool_mutex);
  if (request->client_fd >= 0)
    FD_CLR(request->client_fd, &pool->read_set);
  if (request->server_fd >= 0)
    FD_CLR(request->server_fd, &pool->read_set);
  
  if (request->client_fd == pool->max_fd ||
      request->server_fd == pool->max_fd) {
    pool->max_fd--;
    while (pool->max_fd >= 0) {
      if (FD_ISSET(pool->max_fd, &pool->read_set)) {
        break;
      }
      pool->max_fd--;
    }
  }

  pool->req_num--;
  if (pool->req_num == MAX_REQ-1) {
    /// Increase avail_pools by 1
    pthread_mutex_lock(&avail_pools_mutex);
    avail_pools++;
    pthread_cond_signal(&pools_avail_cond);
    pthread_mutex_unlock(&avail_pools_mutex);
  }

  pool->enabled[index] = 0;
  pthread_mutex_unlock(&pool->pool_mutex);
}

void *WorkThread(void *args) {
  size_t worker_id = (size_t)args;
  struct RequestPool *pool = &request_pools[worker_id];
  int nready = 0;
  int max_fd = -1;
  fd_set ready_set;
  
  struct timeval timeout;   // select waiting time
  timeout.tv_sec = 0;
  timeout.tv_usec = SELECT_TIMEOUT_US;

  while (1) {
    // Get all available file descriptors that can be read
    pthread_mutex_lock(&pool->pool_mutex);
    while (pool->req_num <= 0) {
      /// Wait for pool to be not empty.
      /// [cancel point] This is a pthread cancel point. If thread is
      /// canceled from here, the pool_mutex will remain locked, so 
      /// be careful with that.
      pthread_cond_wait(&pool->pool_empty, &pool->pool_mutex);
    }
    ready_set = pool->read_set;
    max_fd = pool->max_fd;
    pthread_mutex_unlock(&pool->pool_mutex);

    // Call select for I/O multiplexing
    // [cancel point] This is a pthread cancel point.
    nready = select(max_fd+1, &ready_set, NULL, NULL, &timeout);

    // Handle all ready descriptors
    if (nready > 0) {
      int req_ind = GetNextActiveRequestInPool(pool, &ready_set, 0);
      while (req_ind != -1) {
        int retval = 1;
        struct ProxyMeta *request = &pool->requests[req_ind];
        int client_fd = request->client_fd;
        int server_fd = request->server_fd;
        /// Client fd is ready to read
        if (client_fd >= 0 && FD_ISSET(client_fd, &ready_set)) {
          if (request->proxy_state == UNCONNECTED) {
            /// [cancel point] This is a pthread cancel point.
            retval = HandleUnconnectedClientFd(pool, request, worker_id);
          }
          if (request->proxy_state == CONNECTED) {
            /// [cancel point] This is a pthread cancel point.
            retval = HandleConnectedClientFd(request, worker_id);
          }
          if (request->proxy_state == CACHED) {
            /// [cancel point] This is a pthread cancel point.
            retval = HandleCachedClientFd(request, worker_id);
          }
          
          /// if error occurred or proxy finished, close the request
          if (retval <= 0) RmRequestInpool(pool, req_ind);
        }
        /// Server fd is ready to read
        if (retval > 0 && server_fd >= 0 && FD_ISSET(server_fd, &ready_set)) {
          /// [cancel point] This is a pthread cancel point.
          retval = HandleServerFd(request, worker_id);

          /// if error occurred or proxy finished, close the request
          if (retval <= 0) RmRequestInpool(pool, req_ind);
        }

        req_ind = GetNextActiveRequestInPool(pool, &ready_set, req_ind+1);
      }
    }
  }

  return NULL;
}

int GetNextActiveRequestInPool(struct RequestPool *pool,
                               fd_set *ready_setp,
                               int start_index) {
  int ret_ind = -1;

  pthread_mutex_lock(&pool->pool_mutex);
  for (int i = start_index; i < MAX_REQ; i++) {
    if (!pool->enabled[i]) continue;
    int client_fd = pool->requests[i].client_fd;
    int server_fd = pool->requests[i].server_fd;
    if (client_fd >= 0 && FD_ISSET(client_fd, ready_setp)) {
      ret_ind = i;
      break;
    }
    if (server_fd >= 0 && FD_ISSET(server_fd, ready_setp)) {
      ret_ind = i;
      break;
    }
  }
  pthread_mutex_unlock(&pool->pool_mutex);

  return ret_ind;
}

int HandleUnconnectedClientFd(struct RequestPool *pool,
                              struct ProxyMeta *request,
                              size_t worker_id) {
  char line[MAXBUF];
  ssize_t retval;
  char *server_host = NULL;          // host parsed in HttpRequest
  char *server_url = NULL;           // url parsed in HttpRequest
  char host_copy[HOST_LEN];          // host copied from host in HttpRequest
  char *server_hostname = NULL;      // host name extracted from host_copy
  char *server_port = NULL;          // port extracted from host_copy

  do {
    // Read a line from client and parse http fields from the line
    retval = rio_readlineb(&request->client_rp, line, sizeof(line));
    if (retval < 0) {
      /// retval < 0 means that error occurred when reading;
      /// for client_fd in UNCONNECTED state.
      printf("[thread %lu] %s:%s==============>[Unknown] read failed\n",
             worker_id, request->src_host, request->src_port);
      return -1;
    }
    if (retval == 0) {
      /// retval < 0 means that error occurred when reading;
      /// for client_fd in UNCONNECTED state.
      printf("[thread %lu] %s:%s==============>[Unknown] client closed\n",
             worker_id, request->src_host, request->src_port);
      return 0;
    }

    retval = ParseHttpRequest(&request->http_request, line);
    if (retval != 0) {
      printf("[thread %lu] %s:%s==============>[Unknown] http parse error:%s\n",
             worker_id, request->src_host, request->src_port,
             ErrorCodeToMsg(retval));
      return -1;
    }

    // Check if we have got the host information of server.
    if (!IsRequestLineParsed(&request->http_request) ||
        !IsHostParsed(&request->http_request)) {
      continue;
    }

    // Check if proxy url is valid
    if (request->http_request.request_line.proxy_url == NULL) {
      printf("[thread %lu] %s:%s==============>[Unknown] proxy url error\n",
             worker_id, request->src_host, request->src_port);
      return -1;
    }

    server_host = request->http_request.request_headers.host;
    server_url = request->http_request.request_line.proxy_url;
    // Check if the requested url is cached
    if (ENABLE_STATIC_CACHE) {
      retval = CreateCacheInfo(&request->cache_info, server_host, server_url);
      if (retval == 0) {
        if (IsCacheHit(&request->cache_info)) {
          request->proxy_state = CACHED;
          printf("[thread %lu] %s:%s==============>%s%s content cached\n",
                 worker_id, request->src_host, request->src_port,
                 server_host, server_url);
          return 1;
        }
      }
      else {
        printf("[thread %lu] %s:%s==============>%s%s cache error: %s\n",
                 worker_id, request->src_host, request->src_port,
                 server_host, server_url, request->cache_info.error_msg);
      }
    }

    // If not cached, connect to server
    /// Extract server hostname and server port
    strcpy(host_copy, server_host);
    server_hostname = host_copy;
    for (char *ch = host_copy; *ch != '\0'; ch++) {
      if (*ch == ':') {
        *ch = '\0';
        server_port = ch + 1;
        break;
      }
    }
    /// Since it is a http proxy, we use HTTP_PORT by default
    if (!server_port) server_port = HTTP_PORT;

    /// TODO: Check if server is this proxy

    request->server_fd = open_clientfd(server_hostname, server_port);
    if (request->server_fd < 0) {
      printf("[thread %lu] %s:%s==============>%s:%s%s connect failed\n",
             worker_id, request->src_host, request->src_port,
             server_hostname, server_port, server_url);
      return -1;
    }
    // Update pool data
    pthread_mutex_lock(&pool->pool_mutex);
    rio_readinitb(&request->server_rp, request->server_fd);
    FD_SET(request->server_fd, &pool->read_set);
    if (request->server_fd > pool->max_fd) pool->max_fd = request->server_fd;
    pthread_mutex_unlock(&pool->pool_mutex);

    // Send all received request lines from client to server
    for (int i = 0; i < request->http_request.cur_line; i++) {
      char *send_line = request->http_request.origin_lines[i].line;
      if (i == 0) {
        while (!request->http_request.origin_lines[i].line_finish) i++;
        sprintf(line, "%s %s %s\r\n",
                request->http_request.request_line.method,
                request->http_request.request_line.proxy_url,
                request->http_request.request_line.version);
        send_line = line;
      }
      retval = rio_writen(request->server_fd, send_line, strlen(send_line));
      if (retval < 0) {
        printf("[thread %lu] %s:%s==============>%s:%s%s write failed\n",
               worker_id, request->src_host, request->src_port,
               server_hostname, server_port, server_url);
        return -1;
      }
    }

    // Change client_fd state to CONNECTED
    request->proxy_state = CONNECTED;
    printf("[thread %lu] %s:%s==============>%s:%s%s connected\n",
           worker_id, request->src_host, request->src_port,
           server_hostname, server_port, server_url);

    return 1;
  } while (request->client_rp.rio_cnt > 0);
  
  return 1;
}

int HandleConnectedClientFd(struct ProxyMeta *request, size_t worker_id) {
  char line[MAXBUF];
  ssize_t retval;
  char *server_host = NULL;          // host parsed in HttpRequest
  char *server_url = NULL;           // url parsed in HttpRequest

  server_host = request->http_request.request_headers.host;
  server_url = request->http_request.request_line.proxy_url;

  do {
    // Read a line from client, write the line to server
    retval = rio_readlineb(&request->client_rp, line, sizeof(line));
    if (retval < 0) {
      printf("[thread %lu] %s:%s==============>%s%s read failed\n",
             worker_id, request->src_host, request->src_port,
             server_host, server_url);
      return -1;
    }

    if (retval == 0) {
      printf("[thread %lu] %s:%s==============>%s%s client closed\n",
             worker_id, request->src_host, request->src_port,
             server_host, server_url);
      return 0;
    }

    retval = rio_writen(request->server_fd, line, retval);
    if (retval < 0) {
      printf("[thread %lu] %s:%s==============>%s%s write failed\n",
             worker_id, request->src_host, request->src_port,
             server_host, server_url);
      return -1;
    }
  } while (request->client_rp.rio_cnt > 0);

  return 1;
}

int HandleCachedClientFd(struct ProxyMeta *request, size_t worker_id) {
  char line[MAXBUF];
  ssize_t retval;
  char *server_host = NULL;          // host parsed in HttpRequest
  char *server_url = NULL;           // url parsed in HttpRequest

  server_host = request->http_request.request_headers.host;
  server_url = request->http_request.request_line.proxy_url;

  if (!ENABLE_STATIC_CACHE) return 0;

  // Write cache to client
  if (IsCacheError(&request->cache_info)) {
    printf("[thread %lu] %s:%s==============>%s%s cache error: %s\n",
           worker_id, request->src_host, request->src_port,
           server_host, server_url, request->cache_info.error_msg);
    return -1;
  }

  retval = ReadLineFromCache(&request->cache_info, line, sizeof(line));
  while (retval > 0) {
    retval = rio_writen(request->client_fd, line, retval);
    // if (retval < 0) {
    //   printf("[thread %lu] %s:%s<==============%s%s write failed\n",
    //          worker_id, request->src_host, request->src_port,
    //          server_host, server_url);
    //   return -1;
    // }
    retval = ReadLineFromCache(&request->cache_info, line, sizeof(line));
  }

  if (retval < 0) {
    printf("[thread %lu] %s:%s<==============%s%s cache error: %s\n",
           worker_id, request->src_host, request->src_port,
           server_host, server_url, request->cache_info.error_msg);
    return -1;
  }

  printf("[thread %lu] %s:%s<==============%s%s cache success\n",
           worker_id, request->src_host, request->src_port,
           server_host, server_url);
  return 0;
}

int HandleServerFd(struct ProxyMeta *request, size_t worker_id) {
  char line[MAXBUF];
  ssize_t retval;
  ssize_t read_len;
  char *server_host = NULL;          // host parsed in HttpRequest
  char *server_url = NULL;           // url parsed in HttpRequest

  server_host = request->http_request.request_headers.host;
  server_url = request->http_request.request_line.proxy_url;

  do {
    // Read a line from server
    retval = rio_readlineb(&request->server_rp, line, sizeof(line));
    if (retval < 0) {
      printf("[thread %lu] %s:%s<==============%s%s read failed\n",
             worker_id, request->src_host, request->src_port,
             server_host, server_url);
      return -1;
    }
    if (retval == 0) {
      printf("[thread %lu] %s:%s<==============%s%s server closed\n",
             worker_id, request->src_host, request->src_port,
             server_host, server_url);
      return 0;
    }
    read_len = retval;

    // Write the line to cache if possible
    if (ENABLE_STATIC_CACHE) {
      if (!IsCacheError(&request->cache_info)) {
        retval = WriteToCache(&request->cache_info, line, read_len);
        // if (retval < 0) {
        //   printf("[thread %lu] %s:%s<==============%s%s cache error: %s\n",
        //          worker_id, request->src_host, request->src_port,
        //          server_host, server_url, request->cache_info.error_msg);
        // }
      }
    }

    // Write the line to client
    retval = rio_writen(request->client_fd, line, read_len);
    // if (retval < 0) {
    //   printf("[thread %lu] %s:%s<==============%s%s write failed: %s\n",
    //          worker_id, request->src_host, request->src_port,
    //          server_host, server_url, strerror(errno));
    //   return -1;
    // }
  } while (request->server_rp.rio_cnt > 0);

  return 1;
}

void SetExitFlag() {
  atomic_store(&exit_flag, 1);
}

int TestExitFlag() {
  int flag = atomic_load(&exit_flag);
  return flag;
}

void ExitSignalHandler(int sig) {
  // Block all signals
  int olderrno = errno;
  sigset_t mask, prev_mask;
  sigfillset(&mask);
  pthread_sigmask(SIG_BLOCK, &mask, &prev_mask);

  sio_puts("Exit proxy ......\n");
  SetExitFlag();
  close(listenfd);

  // Wakeup main thread if possible
  pthread_cond_signal(&pools_avail_cond);

  // Unblock all signals
  pthread_sigmask(SIG_SETMASK, &prev_mask, NULL);
  errno = olderrno;
}