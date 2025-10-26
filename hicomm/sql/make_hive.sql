-- Metadata stays on InnoDB (for searchable keys)
CREATE DATABASE IF NOT EXISTS hive_meta ENGINE=InnoDB;
CREATE DATABASE IF NOT EXISTS hive_data ENGINE=ROCKSDB;

USE hive_meta;

CREATE TABLE inodes (
    inode_id      BIGINT UNSIGNED NOT NULL PRIMARY KEY AUTO_INCREMENT,
    iname         VARCHAR(255)    NOT NULL,
    iuid          INT UNSIGNED,
    igid          INT UNSIGNED,
    imode         INT UNSIGNED,
    c_time        TIMESTAMP       NULL,
    a_time        TIMESTAMP       NULL,
    m_time        TIMESTAMP       NULL,
    b_time        TIMESTAMP       NULL,
    filename      VARCHAR(255),
    type          CHAR(1),
    UNIQUE KEY u_iname (iname)
) ENGINE=InnoDB;

-- Link table can stay on InnoDB for FK support
CREATE TABLE inode_to_blocks (
    inode_id   BIGINT UNSIGNED NOT NULL,
    block_hash VARBINARY(32)    NOT NULL,
    block_order INT UNSIGNED    NOT NULL,
    PRIMARY KEY (inode_id, block_order),
    KEY idx_block_hash (block_hash),
    CONSTRAINT fk_inode FOREIGN KEY (inode_id) REFERENCES inodes(inode_id)
        ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE host (
    serial             VARCHAR(100) NOT NULL PRIMARY KEY,
    name               VARCHAR(100),
    host_id            VARCHAR(50),
    os_name            VARCHAR(50),
    os_version         VARCHAR(50),
    create_time        DATETIME,
    security_level     INT,
    root_connect_as    INT,
    def_user_connect_as INT,
    ldap_member        BOOLEAN
) ENGINE=InnoDB;

-- 1) Hosts you know about / plan to approve
CREATE TABLE host_auth (
  id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
  machine_uid VARCHAR(128) UNIQUE NOT NULL,   -- your durable ID (UUID from installer), NOT hostname
  name VARCHAR(255),
  serial VARCHAR(255),
  intended_ip VARBINARY(16) NULL,             -- optional; store IPv4/6 in binary
  claims JSON,                                -- any facts you want to record (CPU, TPM EK, etc.)
  status ENUM('pending','approved','revoked') NOT NULL DEFAULT 'pending',
  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  approved_at TIMESTAMP NULL,
  approved_by VARCHAR(128) NULL
) ENGINE=InnoDB;

-- 2) One-time tokens issued by your GUI for first contact
CREATE TABLE host_tokens (
  token CHAR(64) PRIMARY KEY,                 -- 256-bit rand -> hex
  machine_uid VARCHAR(128) NOT NULL,
  expires_at DATETIME NOT NULL,
  used BOOLEAN NOT NULL DEFAULT 0,
  used_at DATETIME NULL,
  UNIQUE KEY u_machine_token (machine_uid, token),
  CONSTRAINT fk_token_machine FOREIGN KEY (machine_uid) REFERENCES host_auth(machine_uid)
      ON DELETE CASCADE
) ENGINE=InnoDB;

-- Remote-facing per-volume superblocks mirrored from the cache disk table.
-- Values correspond to struct hifs_volume_superblock (little-endian on disk;
-- stored here as native MySQL unsigned integers for easy comparison).
CREATE TABLE volume_superblocks (
  volume_id              BIGINT UNSIGNED NOT NULL PRIMARY KEY,
  s_magic                INT UNSIGNED NOT NULL,
  s_blocksize            INT UNSIGNED NOT NULL,
  s_blocksize_bits       INT UNSIGNED NOT NULL,
  s_blocks_count         BIGINT UNSIGNED NOT NULL,
  s_free_blocks          BIGINT UNSIGNED NOT NULL,
  s_inodes_count         BIGINT UNSIGNED NOT NULL,
  s_free_inodes          BIGINT UNSIGNED NOT NULL,
  s_maxbytes             BIGINT UNSIGNED NOT NULL,
  s_feature_compat       INT UNSIGNED NOT NULL,
  s_feature_ro_compat    INT UNSIGNED NOT NULL,
  s_feature_incompat     INT UNSIGNED NOT NULL,
  s_uuid                 BINARY(16) NOT NULL,
  s_rev_level            INT UNSIGNED NOT NULL,
  s_wtime                INT UNSIGNED NOT NULL,
  s_flags                INT UNSIGNED NOT NULL,
  s_volume_name          VARBINARY(16) NOT NULL,
  updated_at             TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                          ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE root_dentries (
  machine_uid           VARCHAR(128) NOT NULL,
  volume_id             BIGINT UNSIGNED NOT NULL,
  rd_inode              BIGINT UNSIGNED NOT NULL,
  rd_mode               INT UNSIGNED NOT NULL,
  rd_uid                INT UNSIGNED NOT NULL,
  rd_gid                INT UNSIGNED NOT NULL,
  rd_flags              INT UNSIGNED NOT NULL,
  rd_size               BIGINT UNSIGNED NOT NULL,
  rd_blocks             BIGINT UNSIGNED NOT NULL,
  rd_atime              INT UNSIGNED NOT NULL,
  rd_mtime              INT UNSIGNED NOT NULL,
  rd_ctime              INT UNSIGNED NOT NULL,
  rd_links              INT UNSIGNED NOT NULL,
  rd_name_len           INT UNSIGNED NOT NULL,
  rd_name               VARBINARY(256) NOT NULL,
  updated_at            TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                         ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (machine_uid, volume_id),
  CONSTRAINT fk_root_machine FOREIGN KEY (machine_uid)
    REFERENCES host_auth(machine_uid) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE volume_dentries (
  volume_id    BIGINT UNSIGNED NOT NULL,
  de_parent    BIGINT UNSIGNED NOT NULL,
  de_inode     BIGINT UNSIGNED NOT NULL,
  de_epoch     INT UNSIGNED NOT NULL,
  de_type      INT UNSIGNED NOT NULL,
  de_name_len  INT UNSIGNED NOT NULL,
  de_name      VARBINARY(256) NOT NULL,
  updated_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (volume_id, de_parent, de_name),
  KEY idx_volume_inode (volume_id, de_inode),
  CONSTRAINT fk_volume_dentry FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE volume_inodes (
  volume_id    BIGINT UNSIGNED NOT NULL,
  inode        BIGINT UNSIGNED NOT NULL,
  inode_blob   VARBINARY(512) NOT NULL,
  epoch        INT UNSIGNED NOT NULL,
  updated_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (volume_id, inode),
  CONSTRAINT fk_volume_inode FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE volume_blocks (
  volume_id    BIGINT UNSIGNED NOT NULL,
  block_no     BIGINT UNSIGNED NOT NULL,
  block_data   VARBINARY(4096) NOT NULL,
  updated_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (volume_id, block_no),
  CONSTRAINT fk_volume_block FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- 3) CSR mailbox (client writes CSR here; controller writes back cert)
CREATE TABLE csr_queue (
  id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
  machine_uid VARCHAR(128) NOT NULL,
  token CHAR(64) NOT NULL,
  csr_pem MEDIUMTEXT NOT NULL,
  submitted_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  status ENUM('received','approved','rejected','issued') NOT NULL DEFAULT 'received',
  status_msg VARCHAR(255) NULL,
  cert_pem MEDIUMTEXT NULL,       -- controller fills
  ca_chain_pem MEDIUMTEXT NULL,   -- controller fills
  cert_serial VARCHAR(128) NULL,  -- controller fills
  not_before DATETIME NULL,
  not_after DATETIME NULL,
  UNIQUE KEY u_machine_open (machine_uid, status),
  CONSTRAINT fk_csr_machine FOREIGN KEY (machine_uid) REFERENCES host_auth(machine_uid)
      ON DELETE CASCADE
) ENGINE=InnoDB;

-- 4) Issued certs log (audit + revocation help)
CREATE TABLE machine_identities (
  id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
  machine_uid VARCHAR(128) NOT NULL,
  cert_serial VARCHAR(128) NOT NULL,
  subject_dn TEXT NOT NULL,
  not_before DATETIME NOT NULL,
  not_after DATETIME NOT NULL,
  revoked BOOLEAN NOT NULL DEFAULT 0,
  revoked_at DATETIME NULL,
  UNIQUE KEY u_cert (cert_serial),
  KEY k_machine (machine_uid),
  CONSTRAINT fk_ident_machine FOREIGN KEY (machine_uid) REFERENCES host_auth(machine_uid)
      ON DELETE CASCADE
) ENGINE=InnoDB;


USE hive_data;

-- RocksDB key/value table: hash -> 4 KB payload
CREATE TABLE blocks (
    block_hash      VARBINARY(128) NOT NULL PRIMARY KEY,  -- 256-bit hash
    block_data           VARBINARY(4096) NOT NULL,
    block_bck_hash  VARBINARY(128)
) ENGINE=ROCKSDB
  COMMENT='rocksdb_cf=block_data';  -- optional: use its own column family



------------------------------------------------------------------------------------
-- Stored procedures for enrolling a new machine and passing it a TLS certificate. -
------------------------------------------------------------------------------------ 
CREATE PROCEDURE sp_bootstrap_begin(
  IN p_machine_uid VARCHAR(128),
  IN p_token CHAR(64),
  IN p_csr_pem MEDIUMTEXT,
  IN p_claims JSON
)
BEGIN
  -- Validate token
  IF NOT EXISTS (
    SELECT 1 FROM host_tokens
     WHERE token = p_token AND machine_uid = p_machine_uid
       AND used = 0 AND expires_at > NOW()
  ) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Invalid or expired token';
  END IF;

  -- Upsert host claims (first-time machine may exist in pending)
  INSERT INTO host_auth (machine_uid, status, claims)
    VALUES (p_machine_uid, 'pending', p_claims)
  ON DUPLICATE KEY UPDATE claims = COALESCE(p_claims, claims);

  -- Place CSR in queue
  INSERT INTO csr_queue (machine_uid, token, csr_pem, status)
    VALUES (p_machine_uid, p_token, p_csr_pem, 'received');

  -- Mark token as used (prevents replay)
  UPDATE host_tokens SET used = 1, used_at = NOW()
    WHERE token = p_token;
END//

CREATE PROCEDURE sp_bootstrap_poll(
  IN p_machine_uid VARCHAR(128)
)
BEGIN
  SELECT id, status, status_msg, cert_pem, ca_chain_pem, cert_serial, not_before, not_after
    FROM csr_queue
   WHERE machine_uid = p_machine_uid
   ORDER BY id DESC
   LIMIT 1;
END//

CREATE PROCEDURE sp_bootstrap_finish(
  IN p_machine_uid VARCHAR(128)
)
BEGIN
  -- Optional: mark host approved once cert has been retrieved by client
  UPDATE host_auth SET status='approved', approved_at = NOW()
   WHERE machine_uid = p_machine_uid AND status <> 'revoked';
END//
