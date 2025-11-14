/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

// This is Sentinel (or more accurately, GUARD since behives have guard bees).
// Guard is a wrapper for the storage hive that directs query traffic,
// acts as a security barrier (being one of few open ports in the hive),
// handles splitting blocks into erasure coding stripes before storage, 
// sharding of the erasure code data into stripes across nodes, and
// lastly, re-balances erasure coding when nodes are added or removed.
//
// Also, some of my least favorite functions: Guard will eventually need to
// handle:
//      Posix read-write locking,
//      close-to-open leases,
//      update & all notifications,
//      key/value store compaction,
//      renaming atomicity,
//      snapshots,
//      and other duties as assigned.
//
// 
// Minimal storage wrapper: TCP server + link keepalive + MariaDB only
// Build: see Makefile below.
// Linux-only (uses epoll). JSON via cJSON (small, embedded) - included inline for brevity.
#include "hive_guard.h" 



// ---- Main server loop --------------------------------------------------------
static volatile sig_atomic_t g_stop=0;
static void on_sigint(int s){ (void)s; g_stop=1; }
 

int main(){
  signal(SIGINT,on_sigint); signal(SIGTERM,on_sigint);

  const char* listen_addr = get_env(ENV_LISTEN_ADDR, "0.0.0.0");
  int listen_port = get_env_int(ENV_LISTEN_PORT, 7400);
  int idle_ms = get_env_int(ENV_IDLE_MS, 30000); 

  const char* m_host = get_env(ENV_MDB_HOST,"127.0.0.1");
  int m_port = get_env_int(ENV_MDB_PORT,3306);
  const char* m_user = get_env(ENV_MDB_USER,"root");
  const char* m_pass = get_env(ENV_MDB_PASS,"");
  const char* m_db   = get_env(ENV_MDB_DB,"storage");
  ServerState S={0}; S.idle_timeout_ms=idle_ms;


  // Open DBs
  if(return 1; }
  if(mdb_open(&S.mdb, m_host, m_port, m_user, m_pass, m_db)!=0){ LOGE("Failed to connect MariaDB"); return 1; }
  LOGI("Connected to MariaDB %s:%d db=%s", m_host, m_port, m_db);


  // Listen
  int sfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sfd<0){ perror("socket"); return 1; }
  int yes=1; setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
  struct sockaddr_in addr={0}; addr.sin_family=AF_INET; addr.sin_port=htons(listen_port);
  addr.sin_addr.s_addr = inet_addr(listen_addr);
  if(bind(sfd,(struct sockaddr*)&addr,sizeof(addr))<0){ perror("bind"); return 1; }
  if(listen(sfd, 128)<0){ perror("listen"); return 1; }
  set_nonblock(sfd);
  LOGI("Listening on %s:%d (idle_timeout=%d ms)", listen_addr, listen_port, idle_ms);

 
  int ep = epoll_create1(0); if(ep<0){ perror("epoll_create1"); return 1; }
  struct epoll_event ev={.events=EPOLLIN, .data.fd=sfd};
  epoll_ctl(ep, EPOLL_CTL_ADD, sfd, &ev);

 
  // For simplicity, store clients in a fixed small array.
  #define MAXC 1024
  Client* clients[MAXC]={0};

  auto int find_slot(void)->int; int find_slot(void){ for(int i=0;i<MAXC;i++) if(!clients[i]) return i; return -1; }
  auto void close_client(int idx)->void; void close_client(int idx){
    Client* c=clients[idx]; if(!c) return;
    epoll_ctl(ep, EPOLL_CTL_DEL, c->fd, NULL); close(c->fd);
    if(c->rbuf) free(c->rbuf);
    free(c); clients[idx]=NULL;
  }

 
  struct epoll_event events[128];
  while(!g_stop){
    int n = epoll_wait(ep, events, 128, 250);
    uint64_t now = now_ms();
 
    for(int i=0;i<n;i++){
      if(events[i].data.fd==sfd){
        // accept
        while(1){
          struct sockaddr_in ca; socklen_t calen=sizeof(ca);
          int cfd = accept(sfd,(struct sockaddr*)&ca,&calen);
          if(cfd<0){ if(errno==EAGAIN||errno==EWOULDBLOCK) break; perror("accept"); break; }
          set_nonblock(cfd);
          int slot = find_slot(); if(slot<0){ LOGW("Too many clients"); close(cfd); continue; }
          Client* c = calloc(1,sizeof(Client)); c->fd=cfd; c->last_active_ms=now;
          clients[slot]=c;
          struct epoll_event cev={.events=EPOLLIN|EPOLLET, .data.u32=(uint32_t)(slot+1)}; // slot+1 avoid 0
          epoll_ctl(ep, EPOLL_CTL_ADD, cfd, &cev);
          LOGI("Client accepted fd=%d slot=%d", cfd, slot);
        }
      } else {
        int slot = (int)events[i].data.u32 - 1;
        if(slot<0 || slot>=MAXC || !clients[slot]) continue;
        Client* c = clients[slot];
        int rc = handle_one(c, &S);
        if(rc!=0){ LOGI("Closing client slot=%d rc=%d", slot, rc); close_client(slot); }
      }
    }

 
    // Idle timeout sweep
    for(int i=0;i<MAXC;i++){
      Client* c=clients[i]; if(!c) continue;
      if(now - c->last_active_ms > (uint64_t)S.idle_timeout_ms){
        LOGI("Idle timeout client slot=%d", i);
        close_client(i);
      }
    }
  }

  LOGI("Shutting down...");
  for(int i=0;i<MAXC;i++) if(clients[i]) close_client(i);
  close(sfd); close(ep);
  mdb_close(&S.mdb);
  return 0;

}
