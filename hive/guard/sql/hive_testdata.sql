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
  volume_id, inode, i_msg_flags, i_version, i_flags, i_mode, i_ino, i_uid, i_gid,
  i_hrd_lnk, i_atime, i_mtime, i_ctime, i_size, i_name,
  i_addrb0, i_addrb1, i_addrb2, i_addrb3,
  i_addre0, i_addre1, i_addre2, i_addre3,
  i_blocks, i_bytes, i_links, i_hash_count, i_hash_reserved, epoch
) VALUES (
  @volume_id, @root_inode,
  0, 1, 0, @root_mode, @root_inode, 0, 0,
  @root_links, @now, @now, @now, 0,
  UNHEX(CONCAT('2f', REPEAT('00', (256 - 1)))),
  0, 0, 0, 0,
  0, 0, 0, 0,
  0, 0, @root_links, 0, 0, @now
) ON DUPLICATE KEY UPDATE
  i_mtime = VALUES(i_mtime),
  epoch = VALUES(epoch);

DELETE FROM hive_meta.volume_inode_fingerprints
 WHERE volume_id = @volume_id AND inode = @root_inode;

-- ----- data: one example block encoded as 4+2 EC stripes -----
-- For testing, we seed six zeroed stripes (k=4, m=2). Parity of all-zero data is also zero,
-- which keeps the fragments trivially decodable.
SET @hash_hex := 'ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7';
SET @stripe1 := NULL; SET @stripe2 := NULL; SET @stripe3 := NULL; SET @stripe4 := NULL;
SET @stripeP1 := NULL; SET @stripeP2 := NULL;

INSERT INTO hive_data.ecblocks (ec_block, estripe_version) VALUES (UNHEX(REPEAT('00', 1024)), '1');
SET @stripe1 := LAST_INSERT_ID();
INSERT INTO hive_data.ecblocks (ec_block, estripe_version) VALUES (UNHEX(REPEAT('00', 1024)), '1');
SET @stripe2 := LAST_INSERT_ID();
INSERT INTO hive_data.ecblocks (ec_block, estripe_version) VALUES (UNHEX(REPEAT('00', 1024)), '1');
SET @stripe3 := LAST_INSERT_ID();
INSERT INTO hive_data.ecblocks (ec_block, estripe_version) VALUES (UNHEX(REPEAT('00', 1024)), '1');
SET @stripe4 := LAST_INSERT_ID();
INSERT INTO hive_data.ecblocks (ec_block, estripe_version) VALUES (UNHEX(REPEAT('00', 1024)), '1');
SET @stripeP1 := LAST_INSERT_ID();
INSERT INTO hive_data.ecblocks (ec_block, estripe_version) VALUES (UNHEX(REPEAT('00', 1024)), '1');
SET @stripeP2 := LAST_INSERT_ID();

-- Map hash → stripe ids
INSERT INTO hive_meta.hash_to_estripes (
  hash_algo, block_hash,
  estripe_1_id, estripe_2_id, estripe_3_id, estripe_4_id,
  estripe_p1_id, estripe_p2_id, block_bck_hash
) VALUES (
  1, UNHEX(@hash_hex),
  @stripe1, @stripe2, @stripe3, @stripe4,
  @stripeP1, @stripeP2, NULL
) ON DUPLICATE KEY UPDATE
  estripe_1_id=VALUES(estripe_1_id), estripe_2_id=VALUES(estripe_2_id),
  estripe_3_id=VALUES(estripe_3_id), estripe_4_id=VALUES(estripe_4_id),
  estripe_p1_id=VALUES(estripe_p1_id), estripe_p2_id=VALUES(estripe_p2_id);

-- Map logical block → hash
INSERT INTO hive_meta.volume_block_mappings (
  volume_id, block_no, hash_algo, block_hash
) VALUES (
  @volume_id, 0, 1, UNHEX(@hash_hex)
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
