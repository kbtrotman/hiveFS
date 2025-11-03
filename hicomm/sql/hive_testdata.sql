-- Minimal seed data for a virtual local HiveFS volume.
-- Assumes hive_meta database is already created via make_hive.sql.

USE hive_meta;

-- Parameters for the test volume.
SET @volume_id := 1;
SET @cache_device := 'local-cache';

-- Root inode constants.
SET @root_inode := 11;
SET @root_mode := 16877; -- 040755 in decimal.
SET @root_flags := 0;
SET @root_size := 0;
SET @root_blocks := 0;
SET @root_links := 2;

-- Timestamps (epoch seconds).
SET @now := UNIX_TIMESTAMP();

-- Zeroed block fingerprint payload (128 entries * 24 bytes).
SET @empty_fingerprints := REPEAT('00', 128 * 24);

-- Zeroed inode blob placeholder (struct hifs_inode_wire is 3440 bytes, doubled for hex).
SET @empty_inode_blob := REPEAT('00', 3440 * 2);

-- Volume superblock mirror.
INSERT INTO volume_superblocks (
    volume_id,
    s_magic,
    s_blocksize,
    s_blocksize_bits,
    s_blocks_count,
    s_free_blocks,
    s_inodes_count,
    s_free_inodes,
    s_maxbytes,
    s_feature_compat,
    s_feature_ro_compat,
    s_feature_incompat,
    s_uuid,
    s_rev_level,
    s_wtime,
    s_flags,
    s_volume_name
) VALUES (
    @volume_id,
    0x1fa7d0d0,
    4096,
    12,
    1024,
    1024,
    1024,
    1023,
    4294967295,
    0,
    0,
    0,
    UNHEX('00000000000000000000000000000000'),
    1,
    @now,
    0,
    UNHEX(LPAD(HEX(CONV(@volume_id, 10, 16)), 32, '0'))
) ON DUPLICATE KEY UPDATE s_wtime = VALUES(s_wtime);

-- Root inode metadata.
INSERT INTO volume_root_dentries (
    volume_id,
    rd_inode,
    rd_mode,
    rd_uid,
    rd_gid,
    rd_flags,
    rd_size,
    rd_blocks,
    rd_atime,
    rd_mtime,
    rd_ctime,
    rd_links,
    rd_name_len,
    rd_name
) VALUES (
    @volume_id,
    @root_inode,
    @root_mode,
    0,
    0,
    @root_flags,
    @root_size,
    @root_blocks,
    @now,
    @now,
    @now,
    @root_links,
    1,
    UNHEX('2f')
) ON DUPLICATE KEY UPDATE rd_mtime = VALUES(rd_mtime);

-- Volume inode table entry (root only, empty payload).
INSERT INTO volume_inodes (
    volume_id,
    inode,
    inode_blob,
    epoch
) VALUES (
    @volume_id,
    @root_inode,
    UNHEX(@empty_inode_blob),
    @now
) ON DUPLICATE KEY UPDATE epoch = VALUES(epoch);

-- No directory entries beyond root (volume_dentries left empty).
-- No blocks inserted for the empty root directory.

-- Optional: mark presence in inodes table in hive_meta for convenience.
INSERT INTO inodes (
    iname,
    iuid,
    igid,
    imode,
    c_time,
    a_time,
    m_time,
    b_time,
    filename,
    type,
    hash_count,
    hash_reserved
) VALUES (
    CONCAT(@cache_device, ':root'),
    0,
    0,
    @root_mode,
    FROM_UNIXTIME(@now),
    FROM_UNIXTIME(@now),
    FROM_UNIXTIME(@now),
    FROM_UNIXTIME(@now),
    '/',
    'd',
    0,
    0
) ON DUPLICATE KEY UPDATE m_time = VALUES(m_time);
