# hiveFS
A Hive Mind Filesystem

HiveFS is just starting. It is a filesystem for Linux and VMWare datastore use, but unlike typical filesystems, it also extends to a protocol layer, making it somewhat like NFS conceptually. There are 3 companents to the "Hive" in general:
1. The central "Hive" is a blade server that runs a storage appliance ISO cd-rom. Once completed, 
   the ISO install will setup an embedded linux system and configure all storage behind the blade
   for use as a blob of storage in the hive. From that point the storage can be managed without
   any traditional storage management (such as zoning, lun management, etc) since it is a
   virtual appliance. All management is done from the hive blade or the end linux host.

2. HiveFS is a kernel module that takes a small local disk and builds a superblock on it. This
   superblock allows the FS to load and then points it to the central hive. There is a small 
   configurable cache locally on the disk also which will act as NVRAM on a storage array acts.
   In other words, in a power outage, the local cache keeps data consistent. Both the superblock
   and NVRAM area can be very small, on the order of less than 1GB.

3. Hi_Command (or Hi_Comm) is a user-space program that runs on the same linux client as HiveFS.
   Hi_Command sets up several queues with the kernel module and accepts data from the kernel to
   be sent over ethernet or Infiniband to the central hive. In other words, its a user space 
   program that implements the protocols necessary to communicate.

So, to this point what we've described is not so dissimilar to setting up a linux blade with NFS and then sharing the filesystem with several clients (our worker bees in this case) as a global filesystem. So, what does hiveFS buy that NFS doesn't? Well, HiveFS is designed with these main things in mind:
1. Global deduplication. Arrays de-dupe generally on a volume level, or possibly on an aggregate or raid group level. HiveFS de-dupes across all storage under its management, and there is no real client limit.
2. Made with Cloud Computing in mind. Not only is the end goal to make it VMWare-aware but also cloud aware. Currently cloud storage is often more expensive than it seems and devoid of any functionality at all. There are storage appliance images that can turn cloud storage into functional virtual storage arrays, but that adds a much more hefty price to the storage. HiveFS is designed to function as a co-lo appliance for cloud, where cloud workloads can access storage from remote co-locations or at a customer's own datacenter. It's management is much easier than an array, flexible enough not to require re-zones, reboots, and other disruptive processes, and the client portions of the software can run in a cloud image.
3. Unlike NFS, a directory structure of the admin's choosing is assigned to each client, and it can be re-configured on the fly without large copies or moves. Data can be firewalled from clients, added to other clients, and subdirectories can be grafted onto other trees instantly. Unlike other filesystems, the root directory is really at the central hive layer, and every client has a sub-directory that acts as its own personal mount point. Meaning the client can only see the data placed in it's branch of the "tree".
4. The Hive itself manages file copies. Although storage can be used underneath that supports replication, the hive software can also manage replication setup, backup copies, file versioning copies, and database testing cycle snapshots. Since it is globally de-dupable, methods need to exist to make sure the data is well-protected to ensure there is always more than one copy of every block.
5. HiveFS acts on a protocol level, so it sees communications with storage on a block level. Any block application can use it. However, the central hive blade server can always share data on a file or object level (NFS< CIFS, S3) to allow for access from other protocols, making hiveFS more verstile than a file protocol like NFS or CIFS.