-- Metadata stays on InnoDB (for searchable keys)
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

-- RocksDB key/value table: hash -> 4 KB payload
CREATE TABLE blocks (
    block_hash      VARBINARY(128) NOT NULL PRIMARY KEY,  -- 256-bit hash
    block_dt        VARBINARY(4096) NOT NULL,
    block_bck_hash  VARBINARY(128)
) ENGINE=ROCKSDB
  COMMENT='rocksdb_cf=block_data';  -- optional: use its own column family

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

CREATE TABLE machines (
    id                 BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
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