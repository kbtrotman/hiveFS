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
SET @empty_fingerprints := REPEAT('00', 128 * 24);
SET @empty_inode_blob := REPEAT('00', 3440 * 2);

-- ----- meta: superblock -----
INSERT INTO hive_meta.volume_superblocks (
  volume_id, s_magic, s_blocksize, s_blocksize_bits,
  s_blocks_count, s_free_blocks, s_inodes_count, s_free_inodes,
  s_maxbytes, s_feature_compat, s_feature_ro_compat, s_feature_incompat,
  s_uuid, s_rev_level, s_wtime, s_flags, s_volume_name
) VALUES (
  @volume_id, 0x1fa7d0d0, 4096, 12,
  1024, 1024, 1024, 1023,
  4294967295, 0, 0, 0,
  UNHEX('00000000000000000000000000000000'),
  1, @now, 0,
  UNHEX(LPAD(HEX(CONV(@volume_id, 10, 16)), 32, '0'))
) ON DUPLICATE KEY UPDATE s_wtime = VALUES(s_wtime);

-- ----- meta: root dentry mirror -----
INSERT INTO hive_meta.volume_root_dentries (
  volume_id, rd_inode, rd_mode, rd_uid, rd_gid, rd_flags, rd_size, rd_blocks,
  rd_atime, rd_mtime, rd_ctime, rd_links, rd_name_len, rd_name
) VALUES (
  @volume_id, @root_inode, @root_mode, 0, 0, @root_flags, @root_size, @root_blocks,
  @now, @now, @now, @root_links, 1, UNHEX('2f')
) ON DUPLICATE KEY UPDATE rd_mtime = VALUES(rd_mtime);

-- ----- meta: volume inode (root) -----
INSERT INTO hive_meta.volume_inodes (
  volume_id, inode, inode_blob, epoch
) VALUES (
  @volume_id, @root_inode, UNHEX(@empty_inode_blob), @now
) ON DUPLICATE KEY UPDATE epoch = VALUES(epoch);

-- ----- meta: convenience inodes row -----
INSERT INTO hive_meta.inodes (
  iname, iuid, igid, imode, c_time, a_time, m_time, b_time,
  filename, type, hash_count, hash_reserved
) VALUES (
  CONCAT(@cache_device, ':root'),
  0, 0, @root_mode,
  FROM_UNIXTIME(@now), FROM_UNIXTIME(@now), FROM_UNIXTIME(@now), FROM_UNIXTIME(@now),
  '/', 'd', 0, 0
) ON DUPLICATE KEY UPDATE m_time = VALUES(m_time);

-- ----- data: one example block -----
INSERT INTO hive_data.blocks (
  hash_algo, block_hash, block_data, block_bck_hash
) VALUES (
  1,
  UNHEX('ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7'),
  UNHEX(REPEAT('00', 4096)),
  NULL
) ON DUPLICATE KEY UPDATE block_data = VALUES(block_data);

-- ----- meta: map block 0 to the hash -----
INSERT INTO hive_meta.volume_block_mappings (
  volume_id, block_no, hash_algo, block_hash
) VALUES (
  @volume_id, 0, 1,
  UNHEX('ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7')
) ON DUPLICATE KEY UPDATE
  hash_algo = VALUES(hash_algo),
  block_hash = VALUES(block_hash);

-- ----- api: GUI virtual nodes (/Global_Root, /Shareables, /Hosts) -----
-- Idempotent creation with LAST_INSERT_ID trick
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
