/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

// Minimal storage wrapper: TCP server + link keepalive + MariaDB + RocksDB
// Build: see Makefile below.
// Linux-only (uses epoll). JSON via cJSON (small, embedded) - included inline for brevity.
#include "hive_guard.h" 

static const char* get_env(const char* k, const char* def) {
  const char* v = getenv(k);
  return (v && *v) ? v : def;
}
 
static int get_env_int(const char* k, int def) {
  const char* v = getenv(k);
  return (v && *v) ? atoi(v) : def;
}
 
// ---- Logging ----------------------------------------------------------------
static void log_ts(const char* lvl, const char* fmt, ...) {
  struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
  struct tm tm; localtime_r(&ts.tv_sec, &tm);
  char buf[64]; strftime(buf, sizeof(buf), "%F %T", &tm);
  fprintf(stderr, "[%s.%03ld] [%s] ", buf, ts.tv_nsec/1000000, lvl);
  va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
  fprintf(stderr, "\n");
}

#define LOGI(...) log_ts("INFO", __VA_ARGS__)
#define LOGW(...) log_ts("WARN", __VA_ARGS__)
#define LOGE(...) log_ts("ERR ", __VA_ARGS__)
 
// ---- Minimal JSON (cJSON single-file embed) ---------------------------------
// To keep this self-contained, a tiny subset of cJSON is embedded.
// May need to expand later for more complex JSON handling.
// BEGIN very small JSON helper (not full cJSON; only what we use)
 

typedef struct JVal {
  enum { JT_NULL, JT_BOOL, JT_NUM, JT_STR, JT_OBJ, JT_ARR } t;
  double num; int boolean;
  char* str;
  struct JField* obj; size_t obj_len;
  struct JVal** arr; size_t arr_len;
} JVal;
 
typedef struct JField { char* key; JVal* val; } JField;
 
static void* jxmalloc(size_t n){ void* p=malloc(n); if(!p){perror("malloc"); exit(1);} return p; }
static char* jxstrdup(const char* s){ size_t n=strlen(s); char* d=jxmalloc(n+1); memcpy(d,s,n+1); return d; }
 
static void jfree(JVal* v){
  if(!v) return;
  if(v->t==JT_STR && v->str) free(v->str);
  if(v->t==JT_OBJ){
    for(size_t i=0;i<v->obj_len;i++){ free(v->obj[i].key); jfree(v->obj[i].val); }
    free(v->obj);
  }
  if(v->t==JT_ARR){
    for(size_t i=0;i<v->arr_len;i++) jfree(v->arr[i]);
    free(v->arr);
  }
  free(v);
}


// Minimal JSON parser for objects with simple fields; strings, numbers, bools.
// Not robust; intended for controlled protocol payloads.
 
typedef struct { const char* s; size_t i, n; } JTok;
static void sp(JTok* t){ while(t->i<t->n && isspace((unsigned char)t->s[t->i])) t->i++; }
static int  pk(JTok* t){ return t->i<t->n ? t->s[t->i] : 0; }
static int  gt(JTok* t){ return t->i<t->n ? t->s[t->i++] : 0; }
 
static char* parse_string(JTok* t){
  if(gt(t)!='"') return NULL;
  char* out=jxmalloc(1); size_t m=1,len=0; out[0]=0;
  while(1){
    if(t->i>=t->n) return NULL;
    int c=gt(t);
    if(c=='"') break;
    if(c=='\\'){
      if(t->i>=t->n) return NULL;
      c=gt(t);
      if(c=='n') c='\n'; else if(c=='t') c='\t'; // minimal
    }
    if(len+1>=m){ m*=2; out=realloc(out,m); }
    out[len++]=c; out[len]=0;
  }
  return out;
}
 

static JVal* parse_value(JTok* t);


static JVal* parse_object(JTok* t){
  if(gt(t)!='{') return NULL;
  JField* fields=NULL; size_t flen=0,fcap=0;
  sp(t);
  if(pk(t)=='}'){ gt(t);
    JVal* v=jxmalloc(sizeof(JVal)); v->t=JT_OBJ; v->obj=fields; v->obj_len=0; return v; }
  while(1){
    sp(t); char* key=parse_string(t); if(!key) return NULL;
    sp(t); if(gt(t)!=':') return NULL;
    sp(t); JVal* val=parse_value(t); if(!val) return NULL;
    if(flen==fcap){ fcap=fcap?fcap*2:4; fields=realloc(fields, fcap*sizeof(JField)); }
    fields[flen].key=key; fields[flen].val=val; flen++;
    sp(t); int c=gt(t); if(c=='}') break; if(c!=',') return NULL;
  }
  JVal* v=jxmalloc(sizeof(JVal)); v->t=JT_OBJ; v->obj=fields; v->obj_len=flen; return v;
}


static JVal* parse_bool(JTok* t){
  if(t->i+4<=t->n && !strncmp(t->s+t->i,"true",4)){ t->i+=4; JVal* v=jxmalloc(sizeof(JVal)); v->t=JT_BOOL; v->boolean=1; return v; }
  if(t->i+5<=t->n && !strncmp(t->s+t->i,"false",5)){ t->i+=5; JVal* v=jxmalloc(sizeof(JVal)); v->t=JT_BOOL; v->boolean=0; return v; }
  return NULL;
}


static JVal* parse_null(JTok* t){
  if(t->i+4<=t->n && !strncmp(t->s+t->i,"null",4)){ t->i+=4; JVal* v=jxmalloc(sizeof(JVal)); v->t=JT_NULL; return v; }
  return NULL;
}


static JVal* parse_number(JTok* t){
  size_t start=t->i; int c=pk(t);
  if(c=='-'||c=='+'){ gt(t); }
  while(isdigit(pk(t))) gt(t);
  if(pk(t)=='.'){ gt(t); while(isdigit(pk(t))) gt(t); }
  if(t->i==start) return NULL;
  char buf[64]; size_t len=t->i-start; if(len>=sizeof(buf)) len=sizeof(buf)-1;
  memcpy(buf, t->s+start, len); buf[len]=0;
  JVal* v=jxmalloc(sizeof(JVal)); v->t=JT_NUM; v->num=strtod(buf,NULL); return v;
}


static JVal* parse_value(JTok* t){
  sp(t);
  int c=pk(t);
  if(c=='{') return parse_object(t);
  if(c=='"'){ JVal* v=jxmalloc(sizeof(JVal)); v->t=JT_STR; v->str=parse_string(t); return v; }
  JVal* b=parse_bool(t); if(b) return b;
  JVal* n=parse_null(t); if(n) return n;
  JVal* num=parse_number(t); if(num) return num;
  return NULL;
}


static JVal* jparse(const char* s, size_t n){ JTok t={s,0,n}; JVal* v=parse_value(&t); if(!v) return NULL; sp(&t); return v; }
static const JVal* jobj_get(const JVal* o, const char* key){
  if(!o||o->t!=JT_OBJ) return NULL;
  for(size_t i=0;i<o->obj_len;i++) if(strcmp(o->obj[i].key,key)==0) return o->obj[i].val;
  return NULL;
}


static const char* jstr(const JVal* v){ return (v && v->t==JT_STR) ? v->str : NULL; }
static long jnum_i(const JVal* v, long def){ return (v && v->t==JT_NUM) ? (long)v->num : def; }
static int jbool(const JVal* v, int def){ return (v && v->t==JT_BOOL) ? v->boolean : def; }


// JSON builder (very small)
static char* jbuild_obj(const char* fmt, ...){
  // fmt mini-language: k:type; where type is s (string), d (int), b (bool), raw (already JSON)
  // Example: "type:s,ts:d" with args ("pong", 123)
  char* out = jxmalloc(256); size_t cap=256,len=0;
  #define APPEND(...) do{ char tmp[256]; int n=snprintf(tmp,sizeof(tmp), __VA_ARGS__); 
    if(len+n+1>cap){ cap = (len+n+1)*2; out=realloc(out,cap);} memcpy(out+len,tmp,n); len+=n; out[len]=0; }while(0)
  APPEND("{");
  va_list ap; va_start(ap, fmt);
  const char* p=fmt; int first=1;
  while(*p){
    while(*p==' '||*p==',') p++;
    if(!*p) break;
    const char* ks=p; while(*p && *p!=':' && *p!=',') p++;
    char key[64]; size_t klen=p-ks; if(klen>=sizeof(key)) klen=sizeof(key)-1; memcpy(key,ks,klen); key[klen]=0;
    if(*p!=':') break; p++;
    const char* ts=p; while(*p && *p!=',' ) p++;
    char type[16]; size_t tlen=p-ts; if(tlen>=sizeof(type)) tlen=sizeof(type)-1; memcpy(type,ts,tlen); type[tlen]=0;
    if(!first) APPEND(","); first=0;
    if(strcmp(type,"s")==0){
      const char* v = va_arg(ap, const char*);
      APPEND("\"%s\":\"%s\"", key, v?v:"");
    }else if(strcmp(type,"d")==0){
      long v = va_arg(ap, long);
      APPEND("\"%s\":%ld", key, v);
    }else if(strcmp(type,"b")==0){
      int v = va_arg(ap, int);
      APPEND("\"%s\":%s", key, v?"true":"false");
    }else if(strcmp(type,"raw")==0){
      const char* v = va_arg(ap, const char*);
      APPEND("\"%s\":%s", key, v?v:"null");
    }else{
      APPEND("\"%s\":null", key);
    }
    if(*p==',') p++;
  }
  va_end(ap);
  APPEND("}");
  #undef APPEND
  return out;
}
// ---- END minimal JSON --------------------------------------------------------


/

 
// ---- MariaDB (C API) --------------------------------------------------------

 
typedef struct {
  MYSQL* conn;
} MDB;

 
static int mdb_open(MDB* m, const char* host, int port, const char* user, const char* pass, const char* db){
  m->conn = mysql_init(NULL);
  if(!m->conn){ LOGE("mysql_init failed"); return -1; }
  mysql_options(m->conn, MYSQL_OPT_CONNECT_TIMEOUT, (const char*)&(int){5});
  if(!mysql_real_connect(m->conn, host, user, pass, db, port, NULL, 0)){
    LOGE("mysql_real_connect: %s", mysql_error(m->conn));
    return -1;
  }
  return 0;
}


static void mdb_close(MDB* m){
  if(m && m->conn) mysql_close(m->conn);
}


static int mdb_ping(MDB* m){ return mysql_ping(m->conn)==0 ? 0 : -1; }
 

static int mdb_exec(MDB* m, const char* sql){
  if(mysql_query(m->conn, sql)!=0){
    LOGE("mysql_query failed: %s", mysql_error(m->conn));
    return -1;
  }
  // Consume results if any
  MYSQL_RES* res = mysql_store_result(m->conn);
  if(res) mysql_free_result(res);
  return 0;
}
 
// ---- Base64 (very small) ----------------------------------------------------
static const char b64tab[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64decode(const char* in, unsigned char** out, size_t* outlen){
  size_t len=strlen(in); if(len%4) return -1;
  size_t olen = len/4*3;
  if(len>=4 && in[len-1]=='=') olen--;
  if(len>=4 && in[len-2]=='=') olen--;
  unsigned char* buf = jxmalloc(olen);
  int T[256]; memset(T,-1,sizeof(T)); for(int i=0;i<64;i++) T[(int)b64tab[i]]=i; T['=']=0;
  size_t j=0; for(size_t i=0;i<len; i+=4){
    int a=T[(int)in[i]], b=T[(int)in[i+1]], c=T[(int)in[i+2]], d=T[(int)in[i+3]];
    if(a<0||b<0||c<0||d<0){ free(buf); return -1; }
    buf[j++] = (a<<2) | (b>>4);
    if(in[i+2]!='=') buf[j++] = (b<<4) | (c>>2);
    if(in[i+3]!='=') buf[j++] = (c<<6) | d;
  }
  *out=buf; *outlen=j; return 0;
}
 
// ---- Networking & framing ---------------------------------------------------
typedef struct Client {
  int fd;
  uint64_t last_active_ms;
  char* rbuf; size_t rcap, rlen;
} Client;
 
static uint64_t now_ms(){
  struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec*1000 + ts.tv_nsec/1000000;
}
 
static int set_nonblock(int fd){
  int f=fcntl(fd,F_GETFL,0); if(f<0) return -1;
  return fcntl(fd,F_SETFL,f|O_NONBLOCK);
}
 
static int write_framed_json(int fd, const char* json){
  uint32_t n = (uint32_t)strlen(json);
  uint32_t be = htonl(n);
  struct iovec vec[2] = {
    { .iov_base=&be, .iov_len=4 },
    { .iov_base=(void*)json, .iov_len=n }
  };
  ssize_t w1 = send(fd, vec[0].iov_base, vec[0].iov_len, 0);
  if(w1!=4) return -1;
  ssize_t w2 = send(fd, vec[1].iov_base, vec[1].iov_len, 0);
  return (w2==(ssize_t)n) ? 0 : -1;
}
 
static int read_frame(Client* c, char** out_json, size_t* out_len){
  // Accumulate header (4 bytes) then body
  while(1){
    // Need header?
    if(c->rlen < 4){
      if(c->rcap - c->rlen < 1024){ c->rcap = c->rcap? c->rcap*2 : 4096; c->rbuf = realloc(c->rbuf, c->rcap); }
      ssize_t r = recv(c->fd, c->rbuf + c->rlen, c->rcap - c->rlen, 0);
      if(r==0) return 1; // peer closed
      if(r<0){ if(errno==EAGAIN||errno==EWOULDBLOCK) return 2; return -1; }
      c->rlen += (size_t)r; continue;
    }
    uint32_t be_len; memcpy(&be_len, c->rbuf, 4);
    uint32_t len = ntohl(be_len);
    if(len>16*1024*1024) return -1; // guard
    if(c->rlen < 4+len){
      if(c->rcap - c->rlen < (size_t)(4+len - c->rlen)){ c->rcap = 4+len; c->rbuf = realloc(c->rbuf, c->rcap); }
      ssize_t r = recv(c->fd, c->rbuf + c->rlen, c->rcap - c->rlen, 0);
      if(r==0) return 1;
      if(r<0){ if(errno==EAGAIN||errno==EWOULDBLOCK) return 2; return -1; }
      c->rlen += (size_t)r; continue;
    }
    char* payload = jxmalloc(len+1);
    memcpy(payload, c->rbuf+4, len); payload[len]=0;
    // shift buffer
    memmove(c->rbuf, c->rbuf+4+len, c->rlen - (4+len));
    c->rlen -= (4+len);
    *out_json = payload; *out_len=len;
    return 0;
  }
}
 
// ---- Command handlers --------------------------------------------------------
typedef struct {
  MDB mdb;
  uint32_t commit_lsn;
  uint32_t version_counter;
  int idle_timeout_ms;
} ServerState;
 
static char* mk_error(const char* code, const char* msg){
  return jbuild_obj("type:s,code:s,message:s", "error", code, msg);
}
 
static int handle_one(Client* c, ServerState* S){
  char* js=NULL; size_t jlen=0;
  int rf = read_frame(c, &js, &jlen);
  if(rf==2) return 0; // no complete frame yet
  if(rf==1) return 1; // closed
  if(rf<0) return -1;
 
  JVal* root = jparse(js, jlen);
  if(!root || root->t!=JT_OBJ){
    char* err = mk_error("bad_json","invalid or non-object json");
    write_framed_json(c->fd, err); free(err);
    free(js); jfree(root); return 0;
  }
  const char* type = jstr(jobj_get(root,"type"));
  if(!type){
    char* err = mk_error("bad_request","missing type");
    write_framed_json(c->fd, err); free(err);
    free(js); jfree(root); return 0;
  }
 
  if(strcmp(type,"hello")==0){
    const char* client_id = jstr(jobj_get(root,"client_id"));
    long ver = jnum_i(jobj_get(root,"version"),1);
    (void)client_id; (void)ver;
    char session_id[32]; snprintf(session_id,sizeof(session_id),"%d", c->fd);
    char* ok = jbuild_obj("type:s,session_id:s,idle_timeout_ms:d","welcome",session_id,S->idle_timeout_ms);
    write_framed_json(c->fd, ok); free(ok);
  } else if(strcmp(type,"ping")==0){
    long ts = jnum_i(jobj_get(root,"ts"), (long)now_ms());
    char* pong = jbuild_obj("type:s,ts:d","pong", ts);
    write_framed_json(c->fd, pong); free(pong);
  } else if(strcmp(type,"db_status")==0){
    int m = mdb_ping(&S->mdb)==0;
    // RocksDB has no ping; do a trivial exists on a reserved key
    int r = rdb_exists(&S->rdb, "__ping__"); if(r<0) r=0; else r=1;
    char* rep = jbuild_obj("type:s,mariadb:s,rocksdb:s","db_status", m?"ok":"down", r?"ok":"down");
    write_framed_json(c->fd, rep); free(rep);
  } else if(strcmp(type,"hash_check")==0){
    const char* h = jstr(jobj_get(root,"hash"));
    if(!h){ char* e=mk_error("bad_request","missing hash"); write_framed_json(c->fd,e); free(e);}
    else {
      int ex = rdb_exists(&S->rdb, h);
      if(ex<0){ char* e=mk_error("db_error","rocksdb error"); write_framed_json(c->fd,e); free(e); }
      else {
        char* rep = jbuild_obj("type:s,hash:s,exists:b","hash_check", h, ex?1:0);
        write_framed_json(c->fd, rep); free(rep);
      }
    }
  } else if(strcmp(type,"store")==0){
    const char* h = jstr(jobj_get(root,"hash"));
    const char* b64 = jstr(jobj_get(root,"data_b64"));
    const char* obj = jstr(jobj_get(root,"obj_id"));
    const char* txn = jstr(jobj_get(root,"txn_id"));
    long seq = jnum_i(jobj_get(root,"seq"),0);
    if(!h || !b64 || !obj || !txn){
      char* e=mk_error("bad_request","missing one of hash,data_b64,obj_id,txn_id");
      write_framed_json(c->fd,e); free(e);
    } else {
      int ex = rdb_exists(&S->rdb, h);
      int dedupe_hit = (ex==1);
      unsigned char* raw=NULL; size_t rlen=0;
      if(!dedupe_hit){
        if(b64decode(b64,&raw,&rlen)!=0){
          char* e=mk_error("bad_request","invalid base64"); write_framed_json(c->fd,e); free(e);
          goto store_done;
        }
        if(rdb_put(&S->rdb, h, (const char*)raw, rlen)!=0){
          char* e=mk_error("db_error","rocksdb put failed"); write_framed_json(c->fd,e); free(e);
          goto store_done;
        }
      }
      // Idempotent metadata upsert (example tables below)
      char sql[1024];
      // chunks(hash PK, refcount, size)
      snprintf(sql,sizeof(sql),
        "INSERT INTO chunks(hash,refcount,size) VALUES('%s',1,%zu) "
        "ON DUPLICATE KEY UPDATE refcount=refcount+1",
        h, dedupe_hit?0UL:rlen);
      if(mdb_exec(&S->mdb, sql)!=0){
        char* e=mk_error("db_error","mariadb chunks upsert failed"); write_framed_json(c->fd,e); free(e);
        goto store_done;
      }

      // objects(obj_id PK, version, updated_at)
      snprintf(sql,sizeof(sql),
        "INSERT INTO objects(object_id,version,updated_at) "
        "VALUES('%s',1,NOW()) ON DUPLICATE KEY UPDATE version=version+1, updated_at=NOW()", obj);
      if(mdb_exec(&S->mdb, sql)!=0){
        char* e=mk_error("db_error","mariadb objects upsert failed"); write_framed_json(c->fd,e); free(e);
        goto store_done;
      }

      // object_chunks(object_id, seq, hash, txn_id UNIQUE per object?)
      snprintf(sql,sizeof(sql),
        "INSERT INTO object_chunks(object_id,seq,hash,txn_id) "
        "VALUES('%s',%ld,'%s','%s') "
        "ON DUPLICATE KEY UPDATE hash=VALUES(hash)", obj, seq, h, txn);
      if(mdb_exec(&S->mdb, sql)!=0){
        char* e=mk_error("db_error","mariadb object_chunks upsert failed"); write_framed_json(c->fd,e); free(e);
        goto store_done;
      }
 
      S->commit_lsn++; S->version_counter++;
      char* ack = jbuild_obj("type:s,hash:s,dedupe_hit:b,commit_lsn:d,version:d",
                             "store_ack", h, dedupe_hit?1:0, (long)S->commit_lsn, (long)S->version_counter);
      write_framed_json(c->fd, ack); free(ack);
store_done:
      if(raw) free(raw);
    }
  } else {
    char* err = mk_error("unknown_type","unsupported type");
    write_framed_json(c->fd, err); free(err);
  }

  c->last_active_ms = now_ms();
  free(js); jfree(root);
  return 0;
}

 

// ---- Main server loop --------------------------------------------------------
static volatile sig_atomic_t g_stop=0;
static void on_sigint(int s){ (void)s; g_stop=1; }
 

int main(){
  signal(SIGINT,on_sigint); signal(SIGTERM,on_sigint);

  const char* listen_addr = get_env(ENV_LISTEN_ADDR, "0.0.0.0");
  int listen_port = get_env_int(ENV_LISTEN_PORT, 7400);
  int idle_ms = get_env_int(ENV_IDLE_MS, 30000); 

  const char* rdb_path = get_env(ENV_RDB_PATH, "./data/rocks");

  const char* m_host = get_env(ENV_MDB_HOST,"127.0.0.1");
  int m_port = get_env_int(ENV_MDB_PORT,3306);
  const char* m_user = get_env(ENV_MDB_USER,"root");
  const char* m_pass = get_env(ENV_MDB_PASS,"");
  const char* m_db   = get_env(ENV_MDB_DB,"storage");
  ServerState S={0}; S.idle_timeout_ms=idle_ms;


  // Open DBs
  if(rdb_open(&S.rdb, rdb_path)!=0){ LOGE("Failed to open RocksDB at %s", rdb_path); return 1; }
  if(mdb_open(&S.mdb, m_host, m_port, m_user, m_pass, m_db)!=0){ LOGE("Failed to connect MariaDB"); return 1; }
  LOGI("Connected to MariaDB %s:%d db=%s; RocksDB=%s", m_host, m_port, m_db, rdb_path);


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

 
  // For simplicity, store clients in a fixed small array; swap for a real map later.
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
  mdb_close(&S.mdb); rdb_close(&S.rdb);
  return 0;

}

 