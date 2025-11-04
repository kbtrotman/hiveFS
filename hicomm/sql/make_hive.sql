-- Optional: keep things strict
-- SET sql_mode='STRICT_ALL_TABLES,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION';

CREATE DATABASE IF NOT EXISTS hive_api;
CREATE DATABASE IF NOT EXISTS hive_meta;
CREATE DATABASE IF NOT EXISTS hive_data;

-- =========================
-- META SCHEMA (hive_meta)
-- =========================
USE hive_meta;

CREATE TABLE inodes (
  inode_id       BIGINT UNSIGNED NOT NULL PRIMARY KEY AUTO_INCREMENT,
  iname          VARCHAR(255)    NOT NULL,
  iuid           INT UNSIGNED,
  igid           INT UNSIGNED,
  imode          INT UNSIGNED,
  c_time         TIMESTAMP NULL,
  a_time         TIMESTAMP NULL,
  m_time         TIMESTAMP NULL,
  b_time         TIMESTAMP NULL,
  filename       VARCHAR(255),
  type           CHAR(1),
  hash_count     SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  hash_reserved  SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  UNIQUE KEY u_iname (iname)
) ENGINE=InnoDB;

-- Mapping inode -> blocks (for FK safety, keep InnoDB)
CREATE TABLE inode_to_blocks (
  inode_id       BIGINT UNSIGNED NOT NULL,
  block_order    INT UNSIGNED    NOT NULL,
  block_no       BIGINT UNSIGNED NOT NULL,
  hash_algo      TINYINT UNSIGNED NOT NULL,
  block_hash     VARBINARY(32)   NOT NULL,
  block_bck_hash VARBINARY(16)   NULL,
  PRIMARY KEY (inode_id, block_order),
  KEY idx_block_hash (hash_algo, block_hash),
  KEY idx_inode_blockno (inode_id, block_no),
  CONSTRAINT fk_inode FOREIGN KEY (inode_id)
    REFERENCES inodes(inode_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE host (
  serial               VARCHAR(100) NOT NULL PRIMARY KEY,
  name                 VARCHAR(100),
  host_id              VARCHAR(50),
  os_name              VARCHAR(50),
  os_version           VARCHAR(50),
  create_time          DATETIME,
  security_level       INT,
  root_connect_as      INT,
  def_user_connect_as  INT,
  ldap_member          BOOLEAN
) ENGINE=InnoDB;

-- Known/approved hosts
CREATE TABLE host_auth (
  id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
  machine_uid VARCHAR(128) NOT NULL UNIQUE,   -- durable ID (e.g., UUID)
  name VARCHAR(255),
  serial VARCHAR(255),
  intended_ip VARBINARY(16) NULL,             -- IPv4/IPv6 in binary
  claims JSON,                                -- arbitrary facts (CPU, TPM EK, etc.)
  status ENUM('pending','approved','revoked') NOT NULL DEFAULT 'pending',
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  approved_at TIMESTAMP NULL,
  approved_by VARCHAR(128) NULL
) ENGINE=InnoDB;

-- One-time tokens for first contact
CREATE TABLE host_tokens (
  token CHAR(64) PRIMARY KEY,                 -- 256-bit hex
  machine_uid VARCHAR(128) NOT NULL,
  expires_at DATETIME NOT NULL,
  used BOOLEAN NOT NULL DEFAULT 0,
  used_at DATETIME NULL,
  UNIQUE KEY u_machine_token (machine_uid, token),
  CONSTRAINT fk_token_machine FOREIGN KEY (machine_uid)
    REFERENCES host_auth(machine_uid) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Volume metadata (superblock mirror)
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
  updated_at             TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                         ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- Root dentry mirror
CREATE TABLE volume_root_dentries (
  volume_id             BIGINT UNSIGNED NOT NULL PRIMARY KEY,
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
  updated_at            TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                         ON UPDATE CURRENT_TIMESTAMP,
  CONSTRAINT fk_volume_root FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Directory entries
CREATE TABLE volume_dentries (
  volume_id    BIGINT UNSIGNED NOT NULL,
  de_parent    BIGINT UNSIGNED NOT NULL,
  de_inode     BIGINT UNSIGNED NOT NULL,
  de_epoch     INT UNSIGNED NOT NULL,
  de_type      INT UNSIGNED NOT NULL,
  de_name_len  INT UNSIGNED NOT NULL,
  de_name      VARBINARY(256) NOT NULL,
  updated_at   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
               ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (volume_id, de_parent, de_name),
  KEY idx_volume_inode (volume_id, de_inode),
  KEY idx_volume_parent (volume_id, de_parent), -- list directory children
  CONSTRAINT fk_volume_dentry FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Inodes per volume (wire struct payload)
CREATE TABLE volume_inodes (
  volume_id    BIGINT UNSIGNED NOT NULL,
  inode        BIGINT UNSIGNED NOT NULL,
  inode_blob   BLOB NOT NULL,                 -- future-proof vs. VARBINARY(4096)
  epoch        BIGINT UNSIGNED NOT NULL,      -- POSIX seconds, safe past 2106
  updated_at   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
               ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (volume_id, inode),
  CONSTRAINT fk_volume_inode FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Per-volume block fingerprint mapping (dedupe aware)
CREATE TABLE volume_block_mappings (
  volume_id    BIGINT UNSIGNED NOT NULL,
  block_no     BIGINT UNSIGNED NOT NULL,
  hash_algo    TINYINT UNSIGNED NOT NULL,
  block_hash   VARBINARY(32) NOT NULL,
  updated_at   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
               ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (volume_id, block_no),
  KEY idx_block_hash (hash_algo, block_hash),
  CONSTRAINT fk_volume_block FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- CSR mailbox (client writes CSR; controller writes cert)
CREATE TABLE csr_queue (
  id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
  machine_uid VARCHAR(128) NOT NULL,
  token CHAR(64) NOT NULL,
  csr_pem MEDIUMTEXT NOT NULL,
  submitted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  status ENUM('received','approved','rejected','issued') NOT NULL DEFAULT 'received',
  status_msg VARCHAR(255) NULL,
  cert_pem MEDIUMTEXT NULL,       -- controller fills
  ca_chain_pem MEDIUMTEXT NULL,   -- controller fills
  cert_serial VARCHAR(128) NULL,  -- controller fills
  not_before DATETIME NULL,
  not_after DATETIME NULL,
  UNIQUE KEY u_machine_open (machine_uid, status),
  CONSTRAINT fk_csr_machine FOREIGN KEY (machine_uid)
    REFERENCES host_auth(machine_uid) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Issued certs log
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
  CONSTRAINT fk_ident_machine FOREIGN KEY (machine_uid)
    REFERENCES host_auth(machine_uid) ON DELETE CASCADE
) ENGINE=InnoDB;

-- =========================
-- DATA SCHEMA (hive_data)
-- =========================
USE hive_data;

-- Requires MyRocks; if unavailable, change to ENGINE=InnoDB.
-- Composite PK prevents collisions if different algorithms yield same 32-byte digest.
CREATE TABLE blocks (
  hash_algo      TINYINT UNSIGNED NOT NULL DEFAULT 1,   -- HIFS_HASH_ALGO_*
  block_hash     VARBINARY(32) NOT NULL,                 -- SHA-256 (or similar)
  block_data     VARBINARY(4096) NOT NULL,
  block_bck_hash VARBINARY(16) NULL,
  PRIMARY KEY (hash_algo, block_hash)
) ENGINE=ROCKSDB
  COMMENT='rocksdb_cf=block_data';

-- =========================
-- PROCEDURES (in hive_meta)
-- =========================
USE hive_meta;
DELIMITER //

CREATE PROCEDURE sp_bootstrap_begin(
  IN p_machine_uid VARCHAR(128),
  IN p_token CHAR(64),
  IN p_csr_pem MEDIUMTEXT,
  IN p_claims JSON
)
BEGIN
  -- Validate token
  IF NOT EXISTS (
    SELECT 1 FROM hive_meta.host_tokens
    WHERE token = p_token
      AND machine_uid = p_machine_uid
      AND used = 0
      AND expires_at > NOW()
  ) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Invalid or expired token';
  END IF;

  -- Upsert host claims (machine may already exist in 'pending')
  INSERT INTO hive_meta.host_auth (machine_uid, status, claims)
    VALUES (p_machine_uid, 'pending', p_claims)
  ON DUPLICATE KEY UPDATE claims = COALESCE(p_claims, claims);

  -- Place CSR in queue
  INSERT INTO hive_meta.csr_queue (machine_uid, token, csr_pem, status)
    VALUES (p_machine_uid, p_token, p_csr_pem, 'received');

  -- Mark token as used
  UPDATE hive_meta.host_tokens
     SET used = 1, used_at = NOW()
   WHERE token = p_token AND machine_uid = p_machine_uid;
END//

CREATE PROCEDURE sp_bootstrap_poll(
  IN p_machine_uid VARCHAR(128)
)
BEGIN
  SELECT id, status, status_msg, cert_pem, ca_chain_pem, cert_serial, not_before, not_after
    FROM hive_meta.csr_queue
   WHERE machine_uid = p_machine_uid
   ORDER BY id DESC
   LIMIT 1;
END//

CREATE PROCEDURE sp_bootstrap_finish(
  IN p_machine_uid VARCHAR(128)
)
BEGIN
  UPDATE hive_meta.host_auth
     SET status='approved', approved_at = NOW()
   WHERE machine_uid = p_machine_uid AND status <> 'revoked';
END//

DELIMITER ;
