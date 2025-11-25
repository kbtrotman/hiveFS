-- Optional: strict mode
-- SET sql_mode='STRICT_ALL_TABLES,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION';

CREATE DATABASE IF NOT EXISTS hive_api;
CREATE DATABASE IF NOT EXISTS hive_meta;
CREATE DATABASE IF NOT EXISTS hive_data;

-- =========================
-- META SCHEMA (hive_meta)
-- =========================
USE hive_meta;

CREATE TABLE IF NOT EXISTS storage_nodes (
  node_id INT PRIMARY KEY,
  node_name VARCHAR(100) NOT NULL,
  node_address VARCHAR(255) NOT NULL,
  node_uid VARCHAR(128) NOT NULL,
  node_serial VARCHAR(100) NOT NULL,
  node_guard_port INT NOT NULL,
  node_data_port INT NOT NULL,
  last_heartbeat TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  hive_version INT NOT NULL,
  hive_patch_level INT NOT NULL,
  ec_group INT NOT NULL,
  fenced BOOLEAN NOT NULL DEFAULT 0,
  last_maintenance TIMESTAMP NULL,
  maintenance_reason VARCHAR(255) NULL,
  maintenance_started_at TIMESTAMP NULL,
  maintenance_ended_at TIMESTAMP NULL,
  date_added_to_cluster TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  storage_capacity_bytes BIGINT UNSIGNED NOT NULL,
  storage_used_bytes BIGINT UNSIGNED NOT NULL,
  storage_reserved_bytes BIGINT UNSIGNED NOT NULL,
  storage_overhead_bytes BIGINT UNSIGNED NOT NULL,
  UNIQUE KEY u_node_name (node_name),
  UNIQUE KEY u_node_address (node_address)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS shard_map (
  shard_id INT PRIMARY KEY,
  node_id INT PRIMARY KEY,
  shard_name VARCHAR(100) NOT NULL,
  storage_node_id INT NOT NULL,
  date_added TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  stripes BIGINT UNSIGNED NOT NULL,
  ec_group INT NOT NULL,
  stripe_id_low BIGINT UNSIGNED NOT NULL,
  stripe_id_high BIGINT UNSIGNED NOT NULL,
  UNIQUE KEY u_shard_name (shard_name),
  CONSTRAINT fk_shard_node FOREIGN KEY (storage_node_id)
    REFERENCES storage_nodes(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS host (
  serial               VARCHAR(100) NOT NULL PRIMARY KEY,
  host_id              VARCHAR(50),
  name                 VARCHAR(100),
  host_address         VARCHAR(255) NOT NULL,
  os_name              VARCHAR(50),
  os_version           VARCHAR(50),
  create_time          DATETIME,
  hicom_port     INT NOT NULL,
  security_level       INT,
  root_connect_as      INT,
  def_user_connect_as  INT,
  quota_bytes        BIGINT UNSIGNED,
  quota_files        BIGINT UNSIGNED,
  epoch            BIGINT NOT NULL,
  fenced           BOOLEAN NOT NULL,
  last_heartbeat TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  date_added_to_cluster TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  ldap_member          BOOLEAN
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS host_auth (
  id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
  serial VARCHAR(255),
  machine_uid VARCHAR(128) NOT NULL UNIQUE,
  name VARCHAR(255),
  pub_key_pem TEXT,
  issued_on TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at DATETIME NOT NULL,
  revoked_at TIMESTAMP NULL,
  revoked_by VARCHAR(128) NULL,
  revocation_reason VARCHAR(255) NULL,
  intended_ip VARBINARY(16) NULL,
  claims JSON,
  status ENUM('pending','approved','revoked') NOT NULL DEFAULT 'pending',
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS host_tokens (
  token CHAR(64) PRIMARY KEY,
  machine_uid VARCHAR(128) NOT NULL,
  issued_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at DATETIME NOT NULL,
  used BOOLEAN NOT NULL DEFAULT 0,
  used_at DATETIME NULL,
  approved_at TIMESTAMP NULL,
  approved_by VARCHAR(128) NULL,
  expired BOOLEAN NOT NULL DEFAULT 0,
  expired_at DATETIME NULL,
  revoked BOOLEAN NOT NULL DEFAULT 0,
  revoked_at DATETIME NULL,
  revoked_by VARCHAR(128) NULL,
  revocation_reason VARCHAR(255) NULL,
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
  inode         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  type          CHAR(1),
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
  KEY u_iname (i_name),
  CONSTRAINT fk_volume_inode FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE volume_xattrs (
  volume_id   BIGINT UNSIGNED NOT NULL,
  inode       BIGINT UNSIGNED NOT NULL,

  -- Namespace for separation of "user.", "system.", "security.", "trusted."
  -- You can either store the raw prefix (e.g. 'user') or an enum-ish SMALLINT.
  ns          TINYINT UNSIGNED NOT NULL,  -- 0=user, 1=system, 2=security, 3=trusted, etc.

  -- Attribute name WITHOUT the "<ns>." prefix, e.g. "comment" from "user.comment"
  name        VARBINARY(255) NOT NULL,

  -- The raw value; binary to support arbitrary payloads
  value       BLOB NOT NULL,

  -- For ordering / debugging / conflict resolution if needed
  created_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                      ON UPDATE CURRENT_TIMESTAMP,

  -- One row per (volume, inode, ns, name)
  PRIMARY KEY (volume_id, inode, ns, name),

  -- Fast listing of all xattrs for an inode
  KEY idx_xattr_inode (volume_id, inode),

  CONSTRAINT fk_xattr_inode
    FOREIGN KEY (volume_id, inode)
    REFERENCES volume_inodes (volume_id, inode)
    ON DELETE CASCADE
) ENGINE=InnoDB
  COMMENT='Extended attributes for inodes (POSIX-like xattrs)';


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

-- Mapping: content hash -> 4 data + 2 parity stripe IDs
CREATE TABLE IF NOT EXISTS hash_to_estripes (
  hash_algo      TINYINT UNSIGNED NOT NULL,   -- e.g., 1 = SHA-256
  block_hash     VARBINARY(32) NOT NULL,      -- 32 bytes for SHA-256
  ref_count      BIGINT UNSIGNED NOT NULL DEFAULT 1,
  estripe_1_id   BIGINT UNSIGNED NOT NULL,
  estripe_2_id   BIGINT UNSIGNED NOT NULL,
  estripe_3_id   BIGINT UNSIGNED NOT NULL,
  estripe_4_id   BIGINT UNSIGNED NOT NULL,
  estripe_p1_id  BIGINT UNSIGNED NOT NULL,
  estripe_p2_id  BIGINT UNSIGNED NOT NULL,
  block_bck_hash VARBINARY(32) NULL,          -- or VARBINARY(32) if SHA-256
  PRIMARY KEY (hash_algo, block_hash),
  KEY idx_hash (hash_algo, block_hash)
  -- (Optional) I can add individual indexes on estripe_*_id if you need reverse lookups
) ENGINE=InnoDB;

-- Mapping: stripe to its fragment locations (shard and node)
CREATE TABLE estripe_locations (
  estripe_id      BIGINT UNSIGNED NOT NULL,
  shard_id        INT NOT NULL,
  storage_node_id INT UNSIGNED NOT NULL,
  block_offset    BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (estripe_id),
  FOREIGN KEY (e_shard_id) REFERENCES shard_map(shard_id)
  FOREIGN KEY (storage_node_id) REFERENCES storage_nodes(node_id)
);


-- =========================
-- DATA SCHEMA (hive_data)
-- =========================
USE hive_data;

-- Stripe payloads (one row per fragment)
CREATE TABLE IF NOT EXISTS ecblocks (
  estripe_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,  -- generate this id then push back in code
  estripe_version BIGINT UNSIGNED NOT NULL,
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

CREATE PROCEDURE sp_bootstrap_finish(IN p_machine_uid VARCHAR(128))
BEGIN
  UPDATE hive_meta.host_auth
     SET status='approved', approved_at = NOW()
   WHERE machine_uid = p_machine_uid AND status <> 'revoked';
END//

DELIMITER ;
