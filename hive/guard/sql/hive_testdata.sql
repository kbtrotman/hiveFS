-- ----- parameters -----
USE hive_meta;
SET @volume_id := 1;
SET @cache_device := 'local-cache';
SET @root_inode := 11;
SET @root_mode := 16877; -- 040755
SET @root_flags := 0;
SET @root_size := 0;
SET @root_blocks := 0;
SET @root_links := 2;
SET @now := UNIX_TIMESTAMP();

SET @cluster_id := 1;  -- we'll use a fixed cluster id for this test data


-- ===== 1) API: cluster row =====
USE hive_api;

INSERT INTO hive_api.cluster (
  cluster_id,
  cluster_name,
  cluster_description,
  cluster_created,
  cluster_updated,
  mgmt_api_port,
  mgmt_api_version,
  cluster_state,
  cluster_health,
  last_health_check,
  last_health_reason,
  cluster_capacity_bytes,
  cluster_used_bytes,
  cluster_reserved_bytes,
  cluster_overhead_bytes,
  cluster_node_count,
  maintenance_window,
  replication_factor,
  owner_team,
  environment,
  tags
) VALUES (
  @cluster_id,
  'test',
  'test cluster for local dev',
  FROM_UNIXTIME(@now),
  FROM_UNIXTIME(@now),
  8000,
  '1.0',
  'active',                     -- valid value for cluster_state ENUM
  'ok',                         -- valid value for cluster_health ENUM
  FROM_UNIXTIME(@now),
  'test data insert',
  0,                            -- cluster_capacity_bytes
  0,                            -- cluster_used_bytes
  0,                            -- cluster_reserved_bytes
  0,                            -- cluster_overhead_bytes
  1,                            -- cluster_node_count
  '8PM - 8AM Fri - Sun.',
  1,                            -- replication_factor
  'test',
  'dev',                        -- environment ENUM('prod','staging','dev','test')
  JSON_OBJECT('type', 'test')   -- valid JSON
)
ON DUPLICATE KEY UPDATE
  cluster_description     = VALUES(cluster_description),
  cluster_updated         = VALUES(cluster_updated),
  cluster_capacity_bytes  = VALUES(cluster_capacity_bytes),
  cluster_used_bytes      = VALUES(cluster_used_bytes),
  cluster_reserved_bytes  = VALUES(cluster_reserved_bytes),
  cluster_overhead_bytes  = VALUES(cluster_overhead_bytes),
  cluster_node_count      = VALUES(cluster_node_count),
  maintenance_window      = VALUES(maintenance_window),
  replication_factor      = VALUES(replication_factor),
  owner_team              = VALUES(owner_team),
  environment             = VALUES(environment),
  tags                    = VALUES(tags);


-- ===== 2) META: storage node (FK to hive_api.cluster) =====
USE hive_meta;

-- ===== 3) META: superblock =====
INSERT INTO hive_meta.volume_superblocks (
  volume_id, s_magic, s_blocksize, s_blocksize_bits,
  s_blocks_count, s_free_blocks, s_inodes_count, s_free_inodes,
  s_maxbytes, s_feature_compat, s_feature_ro_compat, s_feature_incompat,
  s_uuid, s_rev_level, s_wtime, s_flags, s_volume_name
) VALUES (
  @volume_id, 0xEF53, 4096, 12,
  1024*1024, 1024*1024 - 10, 1024*1024, 1024*1024 - 100,
  1<<40, 0, 0, 0,
  UNHEX('00112233445566778899AABBCCDDEEFF'),
  1, @now, 0,
  UNHEX(CONCAT(HEX('HIVEFS'), REPEAT('00', 16 - LENGTH('HIVEFS'))))
) ON DUPLICATE KEY UPDATE
  s_wtime = VALUES(s_wtime),
  s_flags = VALUES(s_flags);


-- ===== 4) META: root dentry mirror =====
INSERT INTO hive_meta.volume_root_dentries (
  volume_id, rd_inode, rd_mode, rd_uid, rd_gid, rd_flags,
  rd_size, rd_blocks, rd_atime, rd_mtime, rd_ctime,
  rd_links, rd_name_len, rd_name
) VALUES (
  @volume_id, @root_inode, @root_mode, 0, 0, @root_flags,
  @root_size, @root_blocks, @now, @now, @now,
  @root_links, 1, UNHEX(CONCAT('2f', REPEAT('00', (256 - 1))))
) ON DUPLICATE KEY UPDATE
  rd_mtime = VALUES(rd_mtime);


-- ===== 5) META: root inode =====
INSERT INTO hive_meta.volume_inodes (
  volume_id, inode, type,
  i_msg_flags, i_version, i_flags, i_mode, i_ino, i_uid, i_gid,
  i_hrd_lnk, i_atime, i_mtime, i_ctime, i_size, i_name,
  extent0_start, extent1_start, extent2_start, extent3_start,
  extent0_count, extent1_count, extent2_count, extent3_count,
  i_blocks, i_bytes, i_links, i_hash_count, i_hash_reserved, epoch
) VALUES (
  @volume_id, @root_inode,
  'd',
  0, 1, 0, @root_mode, @root_inode, 0, 0,
  @root_links, @now, @now, @now, 0,
  UNHEX(CONCAT('2f', REPEAT('00', (255 - 1)))),
  0, 0, 0, 0,
  0, 0, 0, 0,
  0, 0, @root_links, 0, 0, @now
) ON DUPLICATE KEY UPDATE
  i_mtime = VALUES(i_mtime),
  epoch   = VALUES(epoch);


-- ===== 6) API: GUI virtual nodes =====
USE hive_api;

INSERT INTO hive_api.ui_virtual_node
(parent_id, name, node_kind, target_type, target_host, target_dentry, sort_order)
VALUES (NULL, 'Global_Root', 'virtual', 'none', NULL, NULL, 0)
ON DUPLICATE KEY UPDATE id = LAST_INSERT_ID(id);
SET @v_root_id := LAST_INSERT_ID();

INSERT INTO hive_api.ui_virtual_node
(parent_id, name, node_kind, target_type, target_host, target_dentry, sort_order)
VALUES (@v_root_id, 'Shareables', 'virtual', 'none', NULL, NULL, 0)
ON DUPLICATE KEY UPDATE id = LAST_INSERT_ID(id);
SET @v_shareables_id := LAST_INSERT_ID();

INSERT INTO hive_api.ui_virtual_node
(parent_id, name, node_kind, target_type, target_host, target_dentry, sort_order)
VALUES (@v_root_id, 'Hosts', 'virtual', 'none', NULL, NULL, 1)
ON DUPLICATE KEY UPDATE id = LAST_INSERT_ID(id);
SET @v_hosts_id := LAST_INSERT_ID();

INSERT INTO hive_api.ui_virtual_node
(parent_id, name, node_kind, target_type, target_host, target_dentry, sort_order)
VALUES (@v_hosts_id, 'kuma', 'virtual', 'none', 'kuma', NULL, 1)
ON DUPLICATE KEY UPDATE id = LAST_INSERT_ID(id);
SET @v_host1_id := LAST_INSERT_ID();

INSERT INTO hive_api.ui_virtual_node
(parent_id, name, node_kind, target_type, target_host, target_dentry, sort_order)
VALUES (@v_shareables_id, 'Global_Filesystems', 'virtual', 'none', NULL, NULL, 1)
ON DUPLICATE KEY UPDATE id = LAST_INSERT_ID(id);
SET @v_gfs_id := LAST_INSERT_ID();

INSERT INTO hive_api.ui_virtual_node
(parent_id, name, node_kind, target_type, target_host, target_dentry, sort_order)
VALUES (@v_shareables_id, 'NFS_Shares', 'virtual', 'none', NULL, NULL, 1)
ON DUPLICATE KEY UPDATE id = LAST_INSERT_ID(id);
SET @v_nfs_id := LAST_INSERT_ID();

INSERT INTO hive_api.ui_virtual_node
(parent_id, name, node_kind, target_type, target_host, target_dentry, sort_order)
VALUES (@v_shareables_id, 'S3_shares', 'virtual', 'none', NULL, NULL, 1)
ON DUPLICATE KEY UPDATE id = LAST_INSERT_ID(id);
SET @v_s3_id := LAST_INSERT_ID();
