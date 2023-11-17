CREATE TABLE inodes (
    inode_id BIGINT NOT NULL PRIMARY KEY GENERATED ALWAYS AS IDENTITY,
    iname VARCHAR(255) NOT NULL,
    iuid int,
    igid int,
    imode int,
    c_time TIMESTAMP,
    a_time timestamp,
    m_time timestamp,
    b_time timestamp,
    filename VARCHAR(255),
    type char(1)
);

CREATE TABLE blocks (
    block_id BIGINT NOT NULL GENERATED ALWAYS AS IDENTITY,
    block blob(4096) NOT NULL,
    block_hash VARCHAR(128) NOT NULL PRIMARY KEY,
    block_bck_hash VARCHAR(128)
    block BLOB(4096) NOT NULL,
);

CREATE TABLE inode_to_blocks (
    inode_id BIGINT NOT NULL REFERENCES Inodes(inode_id),
    block_id BIGINT NOT NULL REFERENCES Blocks(block_id),
    block_order INT NOT NULL,
    PRIMARY KEY (inode_id, block_id)
);


CREATE TABLE machines (
    id bigint,
    name character varying(100),
    host_id character varying(50),
    os_name character varying(50),
    os_version character varying(50),
    create_time datetime,
    security_level integer,
    root_connect_as integer,
    def_user_connect_as integer,
    ldap_member boolean
);