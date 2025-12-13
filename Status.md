
# Current Project Status:

## Current State:
The 3 components of HiveFS act as a basic filesystem and store data appropriately. De-dupe, hashing, erasure coding, etc, all work.
There is no release ISOs yet. There's still a lot to be done before that point (see below).
The front-end GUI displays the typical datasets, but the workflows for managing the filesystem in earnest haven't been built yet.



## TDB:
1. The number of inode and capacity is mismatched. It's not appropriate 
   to call this a bug since the functionality never existed, but currently
   the free space displayed is calculated by the number of inodes in the
   attached volume. Since space is shared, once quotas are complete, it
   should be from the quota amount.

2. Fill out shard map from kv data. Shard numbers from a mapped table.
   I only use 1 KV shard per node, but that may not always be the case.

3. Add <<STOP>> when k stripes stored for write and ack. IE: a stripe can
   be re-built from only 4 stripe sections, so the cluster can reach consensus
   at 4 and return a write ACK faster.

4. Add <<STOP>> when k stripes found for read. Same as above. Technically the
   algorithm doesn;t need slow nodes to respond.

5. TCP Encryption.

6. Token to certificate for auth.  mTLS authentication, Tokens to setup key pairs 
   at 1st add. (Process of registering an end client.)

7. Re-balancing algorythm to re-balance EC stripes across KV shards when a node
   is added/removed.

8. Finish GUI Interface to API.

9. Complete Posix Locking with cluster leasing (not necessary but need to explore
   fully to ensure write ordering).

10. Once over again to ensure Write fidelity on power outage once write ACK'd.

11. Wire in all stat collection, all updates & all notifications as automated services.

12. Key/value store compaction, avoidance of SSD write amplification.

13. renaming atomicity check.

14. Full snapshot support. Already have the underlying data, just need an interface.

15. Other duties as assigned.