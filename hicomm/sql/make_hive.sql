-- Optional: strict mode
-- SET sql_mode='STRICT_ALL_TABLES,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION';

CREATE DATABASE IF NOT EXISTS hive_api;
CREATE DATABASE IF NOT EXISTS hive_meta;
CREATE DATABASE IF NOT EXISTS hive_data;

-- =========================
-- META SCHEMA (hive_meta)
-- =========================
USE hive_meta;

CREATE TABLE IF NOT EXISTS inodes (
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

CREATE TABLE IF NOT EXISTS inode_to_blocks (
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

CREATE TABLE IF NOT EXISTS host (
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

CREATE TABLE IF NOT EXISTS host_auth (
  id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
  machine_uid VARCHAR(128) NOT NULL UNIQUE,
  name VARCHAR(255),
  serial VARCHAR(255),
  intended_ip VARBINARY(16) NULL,
  claims JSON,
  status ENUM('pending','approved','revoked') NOT NULL DEFAULT 'pending',
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  approved_at TIMESTAMP NULL,
  approved_by VARCHAR(128) NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS host_tokens (
  token CHAR(64) PRIMARY KEY,
  machine_uid VARCHAR(128) NOT NULL,
  expires_at DATETIME NOT NULL,
  used BOOLEAN NOT NULL DEFAULT 0,
  used_at DATETIME NULL,
  UNIQUE KEY u_machine_token (machine_uid, token),
  CONSTRAINT fk_token_machine FOREIGN KEY (machine_uid)
    REFERENCES host_auth(machine_uid) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS volume_superblocks (
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

-- NEW: dentry_id surrogate PK + uniqueness on (volume_id, de_parent, de_name)
CREATE TABLE IF NOT EXISTS volume_dentries (
  dentry_id   BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  volume_id   BIGINT UNSIGNED NOT NULL,
  de_parent   BIGINT UNSIGNED NOT NULL,  -- parent inode number
  de_inode    BIGINT UNSIGNED NOT NULL,  -- this entry's inode number
  de_epoch    INT UNSIGNED NOT NULL,
  de_type     INT UNSIGNED NOT NULL,
  de_name_len INT UNSIGNED NOT NULL,
  de_name     VARBINARY(256) NOT NULL,
  updated_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
              ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (dentry_id),
  UNIQUE KEY uk_vol_parent_name (volume_id, de_parent, de_name),
  KEY idx_volume_inode (volume_id, de_inode),   -- reverse lookup by inode
  -- idx_volume_parent is redundant with the unique key’s left prefix,
  -- so we omit it to save space/write amplification.
  CONSTRAINT fk_volume_dentry FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS volume_root_dentries (
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

CREATE TABLE IF NOT EXISTS volume_inodes (
  volume_id     BIGINT UNSIGNED NOT NULL,
  inode         BIGINT UNSIGNED NOT NULL,
  i_msg_flags   INT UNSIGNED NOT NULL,
  i_version     TINYINT UNSIGNED NOT NULL,
  i_flags       TINYINT UNSIGNED NOT NULL,
  i_mode        INT UNSIGNED NOT NULL,
  i_ino         BIGINT UNSIGNED NOT NULL,
  i_uid         INT UNSIGNED NOT NULL,
  i_gid         INT UNSIGNED NOT NULL,
  i_hrd_lnk     INT UNSIGNED NOT NULL,
  i_atime       INT UNSIGNED NOT NULL,
  i_mtime       INT UNSIGNED NOT NULL,
  i_ctime       INT UNSIGNED NOT NULL,
  i_size        INT UNSIGNED NOT NULL,
  i_name        VARBINARY(256) NOT NULL,
  i_addrb0      INT UNSIGNED NOT NULL,
  i_addrb1      INT UNSIGNED NOT NULL,
  i_addrb2      INT UNSIGNED NOT NULL,
  i_addrb3      INT UNSIGNED NOT NULL,
  i_addre0      INT UNSIGNED NOT NULL,
  i_addre1      INT UNSIGNED NOT NULL,
  i_addre2      INT UNSIGNED NOT NULL,
  i_addre3      INT UNSIGNED NOT NULL,
  i_blocks      INT UNSIGNED NOT NULL,
  i_bytes       INT UNSIGNED NOT NULL,
  i_links       TINYINT UNSIGNED NOT NULL,
  i_hash_count  SMALLINT UNSIGNED NOT NULL,
  i_hash_reserved SMALLINT UNSIGNED NOT NULL,
  epoch         BIGINT UNSIGNED NOT NULL,
  updated_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (volume_id, inode),
  CONSTRAINT fk_volume_inode FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS volume_inode_fingerprints (
  volume_id    BIGINT UNSIGNED NOT NULL,
  inode        BIGINT UNSIGNED NOT NULL,
  fp_index     INT UNSIGNED NOT NULL,
  block_no     INT UNSIGNED NOT NULL,
  hash_algo    TINYINT UNSIGNED NOT NULL,
  block_hash   VARBINARY(32) NOT NULL,
  PRIMARY KEY (volume_id, inode, fp_index),
  CONSTRAINT fk_volume_inode_fp FOREIGN KEY (volume_id, inode)
    REFERENCES volume_inodes(volume_id, inode) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS volume_block_mappings (
  volume_id    BIGINT UNSIGNED NOT NULL,
  block_no     BIGINT UNSIGNED NOT NULL,
  hash_algo    TINYINT UNSIGNED NOT NULL,
  block_hash   VARBINARY(32) NOT NULL,
  block_bck_hash VARBINARY(32) NULL,
  updated_at   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
               ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (volume_id, block_no),
  KEY idx_block_hash (hash_algo, block_hash),
  CONSTRAINT fk_volume_block FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS csr_queue (
  id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
  machine_uid VARCHAR(128) NOT NULL,
  token CHAR(64) NOT NULL,
  csr_pem MEDIUMTEXT NOT NULL,
  submitted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  status ENUM('received','approved','rejected','issued') NOT NULL DEFAULT 'received',
  status_msg VARCHAR(255) NULL,
  cert_pem MEDIUMTEXT NULL,
  ca_chain_pem MEDIUMTEXT NULL,
  cert_serial VARCHAR(128) NULL,
  not_before DATETIME NULL,
  not_after DATETIME NULL,
  UNIQUE KEY u_machine_open (machine_uid, status),
  CONSTRAINT fk_csr_machine FOREIGN KEY (machine_uid)
    REFERENCES host_auth(machine_uid) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS machine_identities (
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

-- Mapping: content hash -> 6 data + 3 parity stripe IDs
CREATE TABLE IF NOT EXISTS hash_to_estripes (
  hash_algo      TINYINT UNSIGNED NOT NULL,   -- e.g., 1 = SHA-256
  block_hash     VARBINARY(32) NOT NULL,      -- 32 bytes for SHA-256
  estripe_1_id   BIGINT UNSIGNED NOT NULL,
  estripe_2_id   BIGINT UNSIGNED NOT NULL,
  estripe_3_id   BIGINT UNSIGNED NOT NULL,
  estripe_4_id   BIGINT UNSIGNED NOT NULL,
  estripe_5_id   BIGINT UNSIGNED NOT NULL,
  estripe_6_id   BIGINT UNSIGNED NOT NULL,
  estripe_p1_id  BIGINT UNSIGNED NOT NULL,
  estripe_p2_id  BIGINT UNSIGNED NOT NULL,
  estripe_p3_id  BIGINT UNSIGNED NOT NULL,
  block_bck_hash VARBINARY(32) NULL,          -- or VARBINARY(32) if SHA-256
  PRIMARY KEY (hash_algo, block_hash),
  KEY idx_hash (hash_algo, block_hash)
  -- (Optional) add individual indexes on estripe_*_id if you need reverse lookups
) ENGINE=InnoDB;


-- =========================
-- DATA SCHEMA (hive_data)
-- =========================
USE hive_data;

-- Stripe payloads (one row per fragment)
CREATE TABLE IF NOT EXISTS ecblocks (
  estripe_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,  -- generate this id then push back in code
  ec_block   BLOB NOT NULL,
  PRIMARY KEY (estripe_id)
) ENGINE=ROCKSDB
  COMMENT='rocksdb_cf=block_data';

-- =========================
-- API SCHEMA (hive_api)
-- =========================
USE hive_api;

CREATE OR REPLACE VIEW v_dentries AS SELECT * FROM hive_meta.volume_dentries;
CREATE OR REPLACE VIEW v_inodes   AS SELECT * FROM hive_meta.inodes;
CREATE OR REPLACE VIEW v_roots    AS SELECT * FROM hive_meta.volume_root_dentries;

-- Virtual nodes for GUI
CREATE TABLE IF NOT EXISTS ui_virtual_node (
  id            BIGINT PRIMARY KEY AUTO_INCREMENT,
  parent_id     BIGINT NULL,
  name          VARCHAR(255) NOT NULL,
  node_kind     ENUM('virtual','mount') NOT NULL,
  target_type   ENUM('none','host','dentry') NOT NULL DEFAULT 'none',
  target_host   VARCHAR(128) NULL,
  target_dentry BIGINT NULL,         -- references hive_meta.volume_dentries.dentry_id (logical)
  sort_order    INT DEFAULT 0,
  UNIQUE KEY uk_ui_virtual_parent_name (parent_id, name),
  KEY idx_ui_virtual_parent (parent_id),
  KEY idx_ui_virtual_kind (node_kind)
) ENGINE=InnoDB;

-- Map a host to a physical root dentry (by surrogate id)
CREATE TABLE IF NOT EXISTS ui_host_map (
  host_id     VARCHAR(128) PRIMARY KEY,
  root_dentry BIGINT NOT NULL,       -- hive_meta.volume_dentries.dentry_id
  KEY idx_ui_host_root (root_dentry)
) ENGINE=InnoDB;

-- Mount -> physical root dentry
CREATE OR REPLACE VIEW v_mounts AS
SELECT
  v.id AS mount_id,
  CASE
    WHEN v.target_type = 'dentry' THEN v.target_dentry
    WHEN v.target_type = 'host'   THEN hm.root_dentry
    ELSE NULL
  END AS root_dentry
FROM hive_api.ui_virtual_node v
LEFT JOIN hive_api.ui_host_map hm
  ON v.target_type = 'host' AND v.target_host = hm.host_id
WHERE v.node_kind = 'mount';

-- Unified tree (virtual + physical using dentry_id)
CREATE OR REPLACE VIEW v_unified_tree AS
-- A) Virtual/UI nodes
SELECT
  CAST(CONCAT('v:', v.id) AS CHAR(64)) AS node_id,
  CASE
    WHEN v.parent_id IS NULL THEN NULL
    ELSE CAST(CONCAT('v:', v.parent_id) AS CHAR(64))
  END AS parent_node_id,
  v.name,
  'virtual' AS node_kind,
  NULL     AS inode_id,
  NULL     AS dentry_id,
  v.id     AS virtual_id,
  m.mount_id IS NOT NULL AS is_mount,
  (
    EXISTS (SELECT 1 FROM hive_api.ui_virtual_node c WHERE c.parent_id = v.id LIMIT 1)
    OR EXISTS (
      SELECT 1
      FROM hive_api.v_mounts mm
      JOIN hive_meta.volume_dentries d ON d.dentry_id = mm.root_dentry
      WHERE mm.mount_id = v.id
      LIMIT 1
    )
  ) AS has_children
FROM hive_api.ui_virtual_node v
LEFT JOIN hive_api.v_mounts m ON m.mount_id = v.id

UNION ALL

-- B) Physical FS nodes. Parent is resolved by linking the parent inode to its dentry row.
SELECT
  CAST(CONCAT('d:', d.dentry_id) AS CHAR(64)) AS node_id,
  CASE
    WHEN mm.mount_id IS NOT NULL
      AND d.de_parent = p.de_inode
      AND p.dentry_id = mm.root_dentry
    THEN CAST(CONCAT('v:', mm.mount_id) AS CHAR(64))
    ELSE CAST(CONCAT('d:', p.dentry_id) AS CHAR(64))
  END AS parent_node_id,
  CONVERT(d.de_name USING utf8mb4) AS name,
  CASE i.type WHEN 'd' THEN 'dir'
              WHEN 'l' THEN 'symlink'
              WHEN 'f' THEN 'file'
              ELSE 'node' END AS node_kind,
  i.inode_id AS inode_id,
  d.dentry_id AS dentry_id,
  NULL AS virtual_id,
  (mm.mount_id IS NOT NULL) AS is_mount,
  EXISTS (
    SELECT 1
    FROM hive_meta.volume_dentries c
    WHERE c.volume_id = d.volume_id
      AND c.de_parent = d.de_inode
    LIMIT 1
  ) AS has_children
FROM hive_meta.volume_dentries d
LEFT JOIN hive_meta.inodes i
  ON i.inode_id = d.de_inode
LEFT JOIN hive_meta.volume_dentries p
  ON p.volume_id = d.volume_id
 AND p.de_inode  = d.de_parent
LEFT JOIN hive_api.v_mounts mm
  ON mm.root_dentry = p.dentry_id
WHERE d.de_parent IS NOT NULL;

-- =========================
-- PROCEDURES (hive_meta)
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
  IF NOT EXISTS (
    SELECT 1 FROM hive_meta.host_tokens
    WHERE token = p_token
      AND machine_uid = p_machine_uid
      AND used = 0
      AND expires_at > NOW()
  ) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Invalid or expired token';
  END IF;

  INSERT INTO hive_meta.host_auth (machine_uid, status, claims)
    VALUES (p_machine_uid, 'pending', p_claims)
  ON DUPLICATE KEY UPDATE claims = COALESCE(p_claims, claims);

  INSERT INTO hive_meta.csr_queue (machine_uid, token, csr_pem, status)
    VALUES (p_machine_uid, p_token, p_csr_pem, 'received');

  UPDATE hive_meta.host_tokens
     SET used = 1, used_at = NOW()
   WHERE token = p_token AND machine_uid = p_machine_uid;
END//

CREATE PROCEDURE sp_bootstrap_poll(IN p_machine_uid VARCHAR(128))
BEGIN
  SELECT id, status, status_msg, cert_pem, ca_chain_pem, cert_serial, not_before, not_after
    FROM hive_meta.csr_queue
   WHERE machine_uid = p_machine_uid
   ORDER BY id DESC
   LIMIT 1;
END//

CREATE PROCEDURE sp_bootstrap_finish(IN p_machine_uid VARCHAR(128))
BEGIN
  UPDATE hive_meta.host_auth
     SET status='approved', approved_at = NOW()
   WHERE machine_uid = p_machine_uid AND status <> 'revoked';
END//

DELIMITER ;
