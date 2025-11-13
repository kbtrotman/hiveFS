
#include "hi_command.h"



static int g_guard_fd = -1;

static long hifs_get_host_id(void)
{
	long id = gethostid();
	if (id == -1) {
		char hostname[256];
		if (gethostname(hostname, sizeof(hostname)) == 0) {
			unsigned long hash = 5381;
			for (const unsigned char *p = (const unsigned char *)hostname; *p; ++p)
				hash = ((hash << 5) + hash) + *p;
			id = (long)hash;
		} else {
			id = 0;
		}
	}
	return id;
}


static char *hifs_get_machine_id(void)
{
	static char id_buf[64];
	FILE *fp = fopen("/etc/machine-id", "r");
	if (fp) {
		if (fgets(id_buf, sizeof(id_buf), fp)) {
			id_buf[strcspn(id_buf, "\n")] = '\0';
			fclose(fp);
			if (id_buf[0] != '\0')
				return id_buf;
		}
		fclose(fp);
	}

	if (gethostname(id_buf, sizeof(id_buf)) == 0)
		return id_buf;

	strcpy(id_buf, "unknown");
	return id_buf;
}


bool hifs_tcp_connect(void) {
    if (g_guard_fd >= 0)
        return true;
    g_guard_fd = dial(HIFS_GUARD_HOST, HIFS_GUARD_PORT_STR);  // from env or config
    return g_guard_fd >= 0;
}


void hifs_tcp_close(void) {
    if (g_guard_fd >= 0) {
        close(g_guard_fd);
        g_guard_fd = -1;
    }
}


static int recv_json(int fd, char *buf, size_t bufsize) {
    ssize_t n = read_line(fd, buf, bufsize);
    if (n < 0) return -1;
    if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';
    return 0;
}


bool hifs_get_hive_host_sbs( char *host_uuid ) 
{
    if (!*host_uuid || *host_uuid == 0)
        return false;
    if (!hifs_tcp_connect())
        return false;

    int fd = g_guard_fd;
    if (send_jsonf(fd,
        "{\"type\":\"get_host\","
        "\"host_uuid\":%llu}",
        
        ((char)host_uuid,) != 0) {
        hifs_tcp_close();
        return false;
    }

    char reply[65536];
    if (recv_json(fd, reply, sizeof(reply)) != 0) {
        hifs_tcp_close();
        return false;
    }
}


bool hifs_get_hive_host_data(char *machine_id)
{


}


bool hifs_volume_super_get(uint64_t volume_id,)
{


}


int register_hive_host(void)
{
	char sql_query[MAX_QUERY_SIZE];
	char ip_address[64] = {0};
	char *hive_mach_id = NULL;
	long hive_host_id = 0;
	struct hostent *host_entry;
	struct in_addr addr;
	struct utsname uts;

	char name[100] = {0};
	if (gethostname(name, sizeof(name)) != 0) {
		hifs_err("gethostname failed: %s\n", strerror(errno));
		return 0;
	}
	hifs_debug("Host name is [%s]\n", name);

	host_entry = gethostbyname(name);
	if (!host_entry) {
		hifs_crit("gethostbyname failed: %s\n", hstrerror(h_errno));
		return 0;
	}

	memcpy(&addr.s_addr, host_entry->h_addr_list[0], host_entry->h_length);
	snprintf(ip_address, sizeof(ip_address), "%s", inet_ntoa(addr));
	hifs_debug("IP address is [%s]\n", ip_address);

	if (uname(&uts) != 0) {
		hifs_err("uname failed: %s\n", strerror(errno));
		return 0;
	}

	hive_mach_id = hifs_get_machine_id();
	hive_host_id = hifs_get_host_id();

	hifs_info("Hive machine ID is [%s] and host ID is [%ld]\n", hive_mach_id, hive_host_id);

	MYSQL_RES *res = hifs_get_hive_host_data(hive_mach_id);
	if (!res) {
		hifs_err("Failed to query hive host data\n");
		return 0;
	}

	MYSQL_ROW row;
	unsigned int row_count = 0;
	mysql_data_seek(res, 0);
	while ((row = mysql_fetch_row(res)) != NULL) {
		row_count++;
		sqldb.host.serial = row[0];
		sqldb.host.name = row[1];
		sqldb.host.host_id = row[2] ? str_to_long(row[2]) : 0;
		sqldb.host.os_name = row[3];
		sqldb.host.os_version = row[4];
		sqldb.host.create_time = row[5];

		hifs_debug("serial: %s, name: %s, host_id: %ld, os: %s %s, created: %s",
			   safe_str(sqldb.host.serial), safe_str(sqldb.host.name),
			   sqldb.host.host_id,
			   safe_str(sqldb.host.os_name),
			   safe_str(sqldb.host.os_version),
			   safe_str(sqldb.host.create_time));
	}

	if (row_count > 0)
		return 1;

	hifs_notice("This host does not exist in the hive. Filesystems cannot be mounted without a hive connection.");
	hifs_notice("Would you like to add this host to the hive? [y/n]\n");

	char response = 'n';
	if (scanf(" %c", &response) != 1 || (response != 'y' && response != 'Y')) {
		hifs_notice("Not registering host to Hive.");
		return 0;
	}

	sqldb.host.serial = hive_mach_id;
	sqldb.host.name = name;
	sqldb.host.host_id = hive_host_id;
	sqldb.host.os_name = uts.sysname;
	sqldb.host.os_version = uts.release;
	sqldb.host.create_time = NULL;

	char *serial_q = hifs_get_quoted_value(hive_mach_id);
	char *name_q = hifs_get_quoted_value(name);
	char *os_name_q = hifs_get_quoted_value(uts.sysname);
	char *os_version_q = hifs_get_quoted_value(uts.release);
	if (!serial_q || !name_q || !os_name_q || !os_version_q) {
		free(serial_q);
		free(name_q);
		free(os_name_q);
		free(os_version_q);
		hifs_err("Out of memory while preparing host registration query");
		return 0;
	}

	snprintf(sql_query, sizeof(sql_query), SQL_HOST_UPSERT,
		 safe_str(serial_q), safe_str(name_q), hive_host_id,
		 safe_str(os_name_q), safe_str(os_version_q));

	free(serial_q);
	free(name_q);
	free(os_name_q);
	free(os_version_q);

	hifs_notice("Registering host to Hive.");
	if (!hifs_insert_sql(sql_query))
		return 0;

	hifs_info("Registered host to hive: name [%s] machine ID [%s] host ID [%ld] "
		  "IP address [%s] OS [%s %s]",
		  name, hive_mach_id, hive_host_id, ip_address,
		  uts.sysname, uts.release);
	return 1;
}



bool hifs_volume_super_get(uint64_t volume_id, struct hifs_volume_superblock *out)
{

}


bool hifs_volume_super_set(uint64_t volume_id, const struct hifs_volume_superblock *vsb)
{

}


bool hifs_root_dentry_load(uint64_t volume_id, struct hifs_volume_root_dentry *out)
{

}


bool hifs_root_dentry_store(uint64_t volume_id, const struct hifs_volume_root_dentry *root)
{

}


bool hifs_volume_dentry_load_by_inode(uint64_t volume_id, uint64_t inode,
				 struct hifs_volume_dentry *out)
{

}


bool hifs_volume_dentry_load_by_name(uint64_t volume_id, uint64_t parent,
				 const char *name_hex, uint32_t name_hex_len,
				 struct hifs_volume_dentry *out)
{

}


bool hifs_volume_dentry_store(uint64_t volume_id,
				 const struct hifs_volume_dentry *dent)
{

}


bool hifs_volume_inode_load(uint64_t volume_id, uint64_t inode,
				 struct hifs_inode_wire *out)
{

}


bool hifs_volume_inode_store(uint64_t volume_id,
			 const struct hifs_inode_wire *inode)
{

}


bool hifs_volume_block_load(uint64_t volume_id, uint64_t block_no,
                            uint8_t *buf, uint32_t *len)
{

}


bool hifs_volume_block_store(uint64_t volume_id, uint64_t block_no,
			     const uint8_t *buf, uint32_t len)
{

}


bool hifs_volume_block_recv(uint64_t volume_id, uint64_t block_no,
                            uint8_t *buf, uint32_t *len)
{
    if (!buf || !len || *len == 0)
        return false;
    if (!hifs_tcp_connect())
        return false;

    int fd = g_guard_fd;
    if (send_jsonf(fd,
        "{\"type\":\"load_block\","
        "\"volume_id\":%llu,"
        "\"block_no\":%llu}",
        (unsigned long long)volume_id,
        (unsigned long long)block_no) != 0) {
        hifs_tcp_close();
        return false;
    }

    char reply[65536];
    if (recv_json(fd, reply, sizeof(reply)) != 0) {
        hifs_tcp_close();
        return false;
    }

    /* You will want to parse JSON: extract data_b64, decode → buf, set *len */
    hifs_debug("load_block reply: %s", reply);
    /* TODO: actually base64-decode into buf and set *len */

    return true;
}


bool hifs_volume_block_send(uint64_t volume_id, uint64_t block_no,
                             const uint8_t *buf, uint32_t len)
{
    if (!buf || len == 0 || len > HIFS_DEFAULT_BLOCK_SIZE)
        return false;
    if (!hifs_tcp_connect())
        return false;

    /* 1) Compute SHA-256 hex as today */
    char hash_hex[SHA256_DIGEST_LENGTH * 2 + 1];
    sha256_hex(buf, len, hash_hex);

    /* 2) Base64 encode the whole block */
    size_t b64_len = 0;
    char *b64 = base64_encode(buf, len, &b64_len);
    if (!b64) {
        hifs_warning("base64 encode OOM");
        return false;
    }

    /* 3) Build and send JSON */
    int fd = g_guard_fd;
    if (send_jsonf(fd,
        "{\"type\":\"store_block\","
        "\"volume_id\":%llu,"
        "\"block_no\":%llu,"
        "\"hash_algo\":\"sha256\","
        "\"hash_hex\":\"%s\","
        "\"data_b64\":\"%s\"}",
        (unsigned long long)volume_id,
        (unsigned long long)block_no,
        hash_hex,
        b64) != 0) {
        free(b64);
        hifs_tcp_close();
        return false;
    }
    free(b64);

    /* 4) Receive reply */
    char reply[65536];
    if (recv_json(fd, reply, sizeof(reply)) != 0) {
        hifs_tcp_close();
        return false;
    }

    /* Optional: parse reply JSON to enforce success / dedupe flags */
    hifs_debug("store_block reply: %s", reply);
    return true;
}


/* ------------------ tiny utils ------------------ */
static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

/* Read a single \n-terminated line from fd into buf (size bytes). Returns length (>=0) or -1 on error/EOF. */
static ssize_t read_line(int fd, char *buf, size_t size) {
    size_t used = 0;
    while (used + 1 < size) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) return -1;          /* EOF */
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        buf[used++] = c;
        if (c == '\n') break;
    }
    buf[used] = '\0';
    return (ssize_t)used;
}

/* Base64 encoder (RFC 4648) */
static char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char *base64_encode(const uint8_t *in, size_t len, size_t *out_len) {
    size_t olen = 4 * ((len + 2) / 3);
    char *out = (char *)malloc(olen + 1);
    if (!out) return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = i < len ? in[i++] : 0;
        uint32_t octet_b = i < len ? in[i++] : 0;
        uint32_t octet_c = i < len ? in[i++] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i - 1 > len) ? '=' : b64_table[(triple >> 6) & 0x3F];
        out[j++] = (i > len) ? '=' : b64_table[triple & 0x3F];

        if (i - 1 > len) out[j - 2] = '=';
        if (i > len) out[j - 1] = '=';
    }
    out[j] = '\0';
    if (out_len) *out_len = j;
    return out;
}

/* Read entire file into memory (malloc’d); returns 0 on success */
static int slurp_file(const char *path, uint8_t **buf, size_t *len) {
    *buf = NULL; *len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    uint8_t *mem = (uint8_t *)malloc((size_t)sz);
    if (!mem) { fclose(f); return -1; }
    if (sz > 0 && fread(mem, 1, (size_t)sz, f) != (size_t)sz) {
        free(mem); fclose(f); return -1;
    }
    fclose(f);
    *buf = mem;
    *len = (size_t)sz;
    return 0;
}

/* ------------------ socket ------------------ */
static int dial(const char *host, const char *port) {
    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family   = AF_UNSPEC;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }
    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) perror("connect");
    return fd;
}

/* Send a full buffer (retry on partial) */
static int send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/* Convenience: send a JSON line built with printf-style formatting */
static int send_jsonf(int fd, const char *fmt, ...) {
    char *line = NULL;
    va_list ap;
    va_start(ap, fmt);
    int n = vasprintf(&line, fmt, ap);
    va_end(ap);
    if (n < 0 || !line) return -1;
    int rc = 0;
    rc = send_all(fd, line, (size_t)n);
    if (rc == 0) rc = send_all(fd, "\n", 1);
    free(line);
    return rc;
}


/* ------------------ commands ------------------ */
static int cmd_hello(int fd, const char *client_id, int version) {
    if (send_jsonf(fd,
        "{\"type\":\"hello\",\"client_id\":\"%s\",\"version\":%d}",
        client_id, version) != 0)
        return -1;
    return recv_and_print(fd);
}

static int cmd_ping(int fd) {
    uint64_t ts = now_ms();
    if (send_jsonf(fd, "{\"type\":\"ping\",\"ts\":%llu}",
        (unsigned long long)ts) != 0)
        return -1;
    return recv_and_print(fd);
}

static int cmd_db_status(int fd) {
    if (send_jsonf(fd, "{\"type\":\"db_status\"}") != 0)
        return -1;
    return recv_and_print(fd);
}

/* hash_check: we no longer compute any hash. The argument is sent as-is. */
static int cmd_hash_check(int fd, const char *hash_or_literal) {
    const char *hash = hash_or_literal;
    if (send_jsonf(fd, "{\"type\":\"hash_check\",\"hash\":\"%s\"}", hash) != 0)
        return -1;
    return recv_and_print(fd);
}

/* store: we no longer compute any hash. Caller must provide --hash. */
static int cmd_store(int fd, const char *path,
                     const char *obj_id, int seq,
                     const char *txn_id, const char *hash) {
    if (!hash) {
        fprintf(stderr,
          "Error: --hash is required for store (client no longer computes hashes).\n");
        return -1;
    }

    uint8_t *raw = NULL;
    size_t   rlen = 0;
    if (slurp_file(path, &raw, &rlen) != 0) {
        fprintf(stderr, "failed to read %s\n", path);
        return -1;
    }

    size_t b64len = 0;
    char *b64 = base64_encode(raw, rlen, &b64len);
    free(raw);
    if (!b64) {
        fprintf(stderr, "base64 OOM\n");
        return -1;
    }

    int rc = send_jsonf(fd,
        "{\"type\":\"store\",\"hash\":\"%s\",\"data_b64\":\"%s\","
        "\"obj_id\":\"%s\",\"seq\":%d,\"txn_id\":\"%s\"}",
        hash, b64, obj_id, seq, txn_id);

    free(b64);
    if (rc != 0) return -1;
    return recv_and_print(fd);
}

/* ------------------ CLI ------------------ */


int parse_protocol( char *recieve ) {
    const char *host = "127.0.0.1";
    const char *port = "7777";


    int fd = dial(host, port);
    if (fd < 0) return 1;

    int rc = 0;

    if (strcmp(cmd, "hello") == 0) {
        const char *client_id = "client-123";
        int version = 1;
        for (; i < argc; ++i) {
            if (strcmp(argv[i], "--client-id") == 0 && i + 1 < argc) {
                client_id = argv[++i];
                continue;
            }
            if (strcmp(argv[i], "--version") == 0 && i + 1 < argc) {
                version = atoi(argv[++i]);
                continue;
            }
        }
        rc = cmd_hello(fd, client_id, version);

    } else if (strcmp(cmd, "ping") == 0) {
        rc = cmd_ping(fd);

    } else if (strcmp(cmd, "db_status") == 0) {
        rc = cmd_db_status(fd);

    } else if (strcmp(cmd, "hash_check") == 0) {
        if (i >= argc) { usage(argv[0]); close(fd); return 2; }
        rc = cmd_hash_check(fd, argv[i]);

    } else if (strcmp(cmd, "store") == 0) {
        if (i >= argc) { usage(argv[0]); close(fd); return 2; }
        const char *path   = argv[i++];
        const char *obj_id = "obj-1";
        int         seq    = 0;
        const char *txn_id = "txn-123";
        const char *hash   = NULL;

        while (i < argc) {
            if (strcmp(argv[i], "--obj-id") == 0 && i + 1 < argc) {
                obj_id = argv[++i];
            } else if (strcmp(argv[i], "--seq") == 0 && i + 1 < argc) {
                seq = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--txn-id") == 0 && i + 1 < argc) {
                txn_id = argv[++i];
            } else if (strcmp(argv[i], "--hash") == 0 && i + 1 < argc) {
                hash = argv[++i];
            }
            ++i;
        }
        rc = cmd_store(fd, path, obj_id, seq, txn_id, hash);

    } else {
        usage(argv[0]);
        rc = 2;
    }

    close(fd);
    return (rc == 0) ? 0 : 1;
}
