-- Optional: strict mode
-- SET sql_mode='STRICT_ALL_TABLES,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION';

CREATE DATABASE IF NOT EXISTS hive_api;
CREATE DATABASE IF NOT EXISTS hive_meta;
-- CREATE DATABASE IF NOT EXISTS hive_data;

-- =========================
-- META SCHEMA (hive_meta)
-- =========================

USE hive_api;

-- Virtual Entities only in the API DB.
CREATE TABLE IF NOT EXISTS cluster (
  cluster_id          INT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
  cluster_name        VARCHAR(100) NOT NULL UNIQUE,
  cluster_description TEXT NULL,
  cluster_created     TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  cluster_updated     TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                                   ON UPDATE CURRENT_TIMESTAMP,
  is_ok BOOLEAN NOT NULL DEFAULT 1,
  off_line BOOLEAN NOT NULL DEFAULT 1,
  mgmt_api_port       INT NULL,
  mgmt_api_version    VARCHAR(32) NULL,
  cluster_state       ENUM('active', 'degraded', 'maintenance', 'offline')
                      NOT NULL DEFAULT 'active',
  cluster_health      ENUM('ok', 'warning', 'critical') 
                      NOT NULL DEFAULT 'ok',
  last_health_check   TIMESTAMP NULL,
  last_health_reason  VARCHAR(255) NULL,
  cluster_capacity_bytes      BIGINT UNSIGNED NULL,
  cluster_used_bytes          BIGINT UNSIGNED NULL,
  cluster_reserved_bytes      BIGINT UNSIGNED NULL,
  cluster_overhead_bytes      BIGINT UNSIGNED NULL,
  cluster_node_count          INT UNSIGNED NULL,
  maintenance_window          VARCHAR(64) NULL,
  replication_factor          TINYINT UNSIGNED NULL,
  owner_team                  VARCHAR(100) NULL,
  environment                 ENUM('prod', 'staging', 'dev', 'test') NULL,
  tags                        JSON NULL
) ENGINE=InnoDB;


USE hive_meta;

-- Physical representations only in the meta layer. This is a security layer.
-- These entities should be read-only from the web API side of things. We only
-- want limited views there.
CREATE TABLE IF NOT EXISTS storage_nodes (
  node_id INT UNSIGNED PRIMARY KEY,
  cluster_id INT UNSIGNED DEFAULT NULL,
  node_name VARCHAR(100) NOT NULL,
  node_address VARCHAR(64) NOT NULL,
  node_uid VARCHAR(128) NOT NULL,
  node_serial VARCHAR(100) NOT NULL,
  node_guard_port INT UNSIGNED NOT NULL,
  node_data_port INT UNSIGNED NOT NULL,
  last_heartbeat TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  hive_version INT NOT NULL,
  hive_patch_level INT NOT NULL,
  fenced BOOLEAN NOT NULL DEFAULT 0,
  down BOOLEAN NOT NULL DEFAULT 0,
  is_ok BOOLEAN NOT NULL DEFAULT 1,
  gui_up BOOLEAN NOT NULL DEFAULT 1,
  api_up BOOLEAN NOT NULL DEFAULT 1,
  hg_up BOOLEAN NOT NULL DEFAULT 1,
  last_maintenance TIMESTAMP NULL,
  maintenance_reason VARCHAR(255) NULL,
  maintenance_started_at TIMESTAMP NULL,
  maintenance_ended_at TIMESTAMP NULL,
  date_added_to_cluster TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  meta_capacity_bytes BIGINT UNSIGNED NOT NULL,
  meta_used_bytes BIGINT UNSIGNED NOT NULL,
  meta_reserved_bytes BIGINT UNSIGNED NOT NULL,
  meta_overhead_bytes BIGINT UNSIGNED NOT NULL,
  storage_capacity_bytes BIGINT UNSIGNED NOT NULL,
  storage_used_bytes BIGINT UNSIGNED NOT NULL,
  storage_reserved_bytes BIGINT UNSIGNED NOT NULL,
  storage_overhead_bytes BIGINT UNSIGNED NOT NULL,
  client_connect_timout INT DEFAULT '60000',
  sn_connect_timeout INT DEFAULT '30000',
  last_uptime VARCHAR(25) DEFAULT NULL,
  UNIQUE KEY u_node_name (node_name),
  UNIQUE KEY u_node_address (node_address),

  -- index on cluster_id (InnoDB will create one automatically for the FK,
  -- but it's good to be explicit)
  KEY idx_storage_nodes_cluster_id (cluster_id),

  CONSTRAINT fk_storage_nodes_cluster
    FOREIGN KEY (cluster_id)
    REFERENCES hive_api.cluster (cluster_id)
    ON UPDATE CASCADE
    ON DELETE RESTRICT
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS storage_node_stats (
  node_id INT UNSIGNED NOT NULL,
  s_ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

  cpu INT UNSIGNED,
  mem_used INT UNSIGNED,
  mem_avail INT UNSIGNED,

  read_iops INT UNSIGNED,
  write_iops INT UNSIGNED,
  total_iops INT UNSIGNED,

  meta_chan_ps INT UNSIGNED,

  incoming_mbps INT UNSIGNED,
  cl_outgoing_mbps INT UNSIGNED,

  sn_node_in_mbps INT UNSIGNED,
  sn_node_out_mbps INT UNSIGNED,

  writes_mbps INT UNSIGNED,
  reads_mbps INT UNSIGNED,
  t_throughput INT UNSIGNED,

  c_net_in INT UNSIGNED,
  c_net_out INT UNSIGNED,
  s_net_in INT UNSIGNED,
  s_net_out INT UNSIGNED,

  avg_wr_latency INT UNSIGNED,
  avg_rd_latency INT UNSIGNED,

  lavg VARCHAR(10) DEFAULT NULL,

  sees_warning BOOLEAN NOT NULL DEFAULT 0,
  sees_error BOOLEAN NOT NULL DEFAULT 0,
  message VARCHAR(200) DEFAULT NULL,

  cont1_isok BOOLEAN NOT NULL DEFAULT 0,
  cont2_isok BOOLEAN NOT NULL DEFAULT 0,
  cont1_message VARCHAR(200) DEFAULT NULL,
  cont2_message VARCHAR(200) DEFAULT NULL,

  clients INT UNSIGNED,

  PRIMARY KEY (node_id, s_ts),
  KEY idx_ts (s_ts)
) ENGINE=InnoDB;


CREATE TABLE IF NOT EXISTS alerts (
  id INT UNSIGNED KEY,
  a_lvl INT UNSIGNED DEFAULT 0,
  a_class INT UNSIGNED DEFAULT 0,
  a_comp INT UNSIGNED DEFAULT 0,
  a_msg VARCHAR(200) DEFAULT NULL,
  a_desc VARCHAR(200) DEFAULT NULL,
  a_ts TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS shard_map (
  shard_id INT UNSIGNED PRIMARY KEY,
  node_id INT UNSIGNED NOT NULL,
  shard_name VARCHAR(100) NOT NULL,
  storage_node_id INT UNSIGNED NOT NULL,
  date_added TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  stripes BIGINT UNSIGNED NOT NULL,
  ec_group INT UNSIGNED NOT NULL,
  stripe_id_low BIGINT UNSIGNED NOT NULL,
  stripe_id_high BIGINT UNSIGNED NOT NULL,
  UNIQUE KEY u_shard_name (shard_name),
  CONSTRAINT fk_shard_node FOREIGN KEY (storage_node_id)
    REFERENCES storage_nodes(node_id) ON DELETE CASCADE
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
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
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

-- Directory entries per volume (logical namespace)
CREATE TABLE IF NOT EXISTS volume_dentries (
  dentry_id   BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  volume_id   BIGINT UNSIGNED NOT NULL,
  de_parent   BIGINT UNSIGNED NOT NULL,  -- parent inode (directory)
  de_inode    BIGINT UNSIGNED NOT NULL,  -- target inode
  de_epoch    INT UNSIGNED NOT NULL,
  de_type     INT UNSIGNED NOT NULL,
  de_name_len SMALLINT UNSIGNED NOT NULL,
  de_name     VARBINARY(255) NOT NULL,
  updated_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                         ON UPDATE CURRENT_TIMESTAMP,
  KEY idx_volume_parent (volume_id, de_parent),
  KEY idx_volume_inode  (volume_id, de_inode),
  CONSTRAINT fk_volume_dentry FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Root dentry per volume (mount point inside the volume)
CREATE TABLE IF NOT EXISTS volume_root_dentries (
  volume_id   BIGINT UNSIGNED NOT NULL,
  rd_inode    BIGINT UNSIGNED NOT NULL,
  rd_uid      INT UNSIGNED NOT NULL,
  rd_gid      INT UNSIGNED NOT NULL,
  rd_flags    INT UNSIGNED NOT NULL,
  rd_mode     INT UNSIGNED NOT NULL,
  rd_size     BIGINT UNSIGNED NOT NULL,
  rd_blocks   BIGINT UNSIGNED NOT NULL,
  rd_atime    INT UNSIGNED NOT NULL,
  rd_mtime    INT UNSIGNED NOT NULL,
  rd_ctime    INT UNSIGNED NOT NULL,
  rd_links    INT UNSIGNED NOT NULL,
  rd_name_len INT UNSIGNED NOT NULL,
  rd_name     VARBINARY(256) NOT NULL,
  updated_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                         ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (volume_id),
  CONSTRAINT fk_volume_root FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Inode metadata per volume (used for stats and attributes)
CREATE TABLE IF NOT EXISTS volume_inodes (
  inode         BIGINT UNSIGNED NOT NULL,
  volume_id     BIGINT UNSIGNED NOT NULL,
  type          CHAR(1),
  i_name         VARBINARY(256) NOT NULL, -- inode name (for quick lookup)
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
  i_size        BIGINT UNSIGNED NOT NULL,
  extent0_start INT UNSIGNED NOT NULL,
  extent1_start INT UNSIGNED NOT NULL,
  extent2_start INT UNSIGNED NOT NULL,
  extent3_start INT UNSIGNED NOT NULL,
  extent0_count INT UNSIGNED NOT NULL,
  extent1_count INT UNSIGNED NOT NULL,
  extent2_count INT UNSIGNED NOT NULL,
  extent3_count INT UNSIGNED NOT NULL,
  i_blocks      INT UNSIGNED NOT NULL,
  i_bytes       INT UNSIGNED NOT NULL,
  i_links       TINYINT UNSIGNED NOT NULL,
  i_hash_count  SMALLINT UNSIGNED NOT NULL,
  i_hash_reserved SMALLINT UNSIGNED NOT NULL,
  epoch         BIGINT UNSIGNED NOT NULL,
  tags          JSON NULL,  -- MariaDB supports JSON to store arbitrary labels
  updated_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                         ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (volume_id, inode),
  KEY idx_name (i_ino),
  CONSTRAINT fk_volume_inode FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS block_stripe_locations (
    volume_id  BIGINT NOT NULL,
    block_no   BIGINT NOT NULL,
    hash_algo  TINYINT NOT NULL,
    block_hash BINARY(16) NOT NULL,
    stripe_index TINYINT NOT NULL,  -- 0..5 for k+m
    storage_node_id INT NOT NULL,
    shard_id  INT NOT NULL,
    estripe_id BIGINT NOT NULL,
    block_offset BIGINT NOT NULL,
    ref_count INT UNSIGNED NOT NULL DEFAULT 1,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (volume_id, block_no, stripe_index),
    KEY idx_hash (hash_algo, block_hash)
) ENGINE=InnoDB;

-- Extended attributes per inode (optional GUI exposure)
CREATE TABLE IF NOT EXISTS volume_xattrs (
  volume_id   BIGINT UNSIGNED NOT NULL,
  inode       BIGINT UNSIGNED NOT NULL,
  ns          TINYINT UNSIGNED NOT NULL,  -- namespace (user/system/etc)
  name        VARBINARY(255) NOT NULL,
  value       BLOB NOT NULL,
  updated_at  TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                         ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (volume_id, inode, ns, name),
  CONSTRAINT fk_xattr_inode FOREIGN KEY (volume_id, inode)
    REFERENCES volume_inodes(volume_id, inode) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS volume_stats (
  volume_id    BIGINT UNSIGNED NOT NULL PRIMARY KEY,
  file_count   BIGINT UNSIGNED NOT NULL DEFAULT 0,
  dir_count    BIGINT UNSIGNED NOT NULL DEFAULT 0,
  total_bytes  BIGINT UNSIGNED NOT NULL DEFAULT 0,
  updated_at   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                         ON UPDATE CURRENT_TIMESTAMP,
  CONSTRAINT fk_volume_stats FOREIGN KEY (volume_id)
    REFERENCES volume_superblocks(volume_id) ON DELETE CASCADE
) ENGINE=InnoDB;


-- =========================
-- API SCHEMA (hive_api)
-- =========================
USE hive_api;

CREATE OR REPLACE VIEW v_dentries AS SELECT * FROM hive_meta.volume_dentries;
CREATE OR REPLACE VIEW v_inodes   AS SELECT * FROM hive_meta.volume_inodes;
CREATE OR REPLACE VIEW v_roots    AS SELECT * FROM hive_meta.volume_root_dentries;
CREATE OR REPLACE VIEW v_nodes    AS SELECT * FROM hive_meta.storage_nodes;
CREATE OR REPLACE VIEW v_vstats    AS SELECT * FROM hive_meta.volume_stats;
CREATE OR REPLACE VIEW v_stats    AS SELECT * FROM hive_meta.storage_node_stats;

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
  NULL     AS inode,
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
  i.inode AS inode,
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
LEFT JOIN hive_meta.volume_inodes i
  ON i.inode = d.de_inode
LEFT JOIN hive_meta.volume_dentries p
  ON p.volume_id = d.volume_id
 AND p.de_inode  = d.de_parent
LEFT JOIN hive_api.v_mounts mm
  ON mm.root_dentry = p.dentry_id
WHERE d.de_parent IS NOT NULL;

CREATE OR REPLACE
VIEW v_cluster_overview AS
SELECT
  c.cluster_id,
  c.cluster_name,
  c.cluster_description,
  c.cluster_state,
  c.cluster_health,
  c.cluster_created,
  c.cluster_updated,

  COUNT(sn.node_id)                             AS node_count,
  COALESCE(SUM(sn.storage_capacity_bytes), 0)   AS total_capacity_bytes,
  COALESCE(SUM(sn.storage_used_bytes), 0)       AS total_used_bytes,
  COALESCE(SUM(sn.storage_reserved_bytes), 0)   AS total_reserved_bytes,
  COALESCE(SUM(sn.storage_overhead_bytes), 0)   AS total_overhead_bytes

FROM hive_api.cluster c
LEFT JOIN hive_meta.storage_nodes sn
       ON sn.cluster_id = c.cluster_id
GROUP BY c.cluster_id;

CREATE OR REPLACE
VIEW v_cluster_nodes AS
SELECT
  sn.node_id,
  sn.cluster_id,
  c.cluster_name,
  sn.node_name,
  sn.node_address,
  sn.hive_version,
  sn.hive_patch_level,
  sn.fenced,
  sn.last_heartbeat,
  sn.storage_capacity_bytes,
  sn.storage_used_bytes,
  sn.storage_reserved_bytes,
  sn.storage_overhead_bytes
FROM hive_meta.storage_nodes sn
JOIN hive_api.cluster c
  ON c.cluster_id = sn.cluster_id;

CREATE OR REPLACE
VIEW v_volumes AS
SELECT
  vsb.volume_id,
  CONVERT(vsb.s_volume_name USING utf8mb4) AS volume_name,
  vsb.s_blocksize            AS block_size,
  vsb.s_blocks_count         AS total_blocks,
  vsb.s_free_blocks          AS free_blocks,
  vs.total_bytes           AS total_bytes,   -- if you want, or derive this
  vs.file_count,
  vs.dir_count,
  vs.updated_at              AS stats_updated_at
FROM hive_meta.volume_superblocks vsb
LEFT JOIN hive_meta.volume_stats vs
  ON vs.volume_id = vsb.volume_id;

CREATE OR REPLACE
VIEW v_global_fs_stats AS
SELECT
  COUNT(DISTINCT volume_id)           AS volume_count,
  COALESCE(SUM(file_count), 0)        AS total_files,
  COALESCE(SUM(dir_count), 0)         AS total_dirs,
  COALESCE(SUM(total_bytes), 0)       AS total_bytes
FROM hive_meta.volume_stats;


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
