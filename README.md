# hiveFS
A Hive Mind Filesystem

HiveFS is about 15% toward a first alpha release. It is a filesystem for Linux and VMWare datastore use 
primarily, but unlike typical filesystems, it also extends to a protocol layer, making it somewhat like 
NFS conceptually. There are 3 companents to the "Hive" in general:
1. The central "Hive" is a blade server that runs a storage appliance ISO cd-rom. Once completed, 
   the ISO install will setup an embedded linux system and configure all storage behind the blade
   for use as a blob of storage in the hive. From that point the storage can be managed without
   any traditional storage management (such as zoning, lun management, etc) since it is a
   virtual appliance. All management is done from the hive blade GUI or the end linux host CLI.

2. HiveFS is a kernel module that takes a small local disk and builds a superblock on it and cache.
   The superblock allows the FS to load and then points it to the central hive for inode data. A small 
   configurable cache locally on the disk acts as NVRAM on a storage array acts. In other words, in 
   a power outage, the local cache keeps data consistent. Both the superblock and NVRAM area can be 
   very small, on the order of less than 1GB for typical use.

3. Hi_Command is a user-space program that runs on the same linux client as HiveFS. Hi_Command sets 
   up several queues with the kernel module and accepts data from the kernel to be sent over 
   ethernet or Infiniband to the central hive. In other words, its a user space program that 
   implements the protocols necessary to communicate. The idea here is to loose the slow-down
   inherant in the export of data from the linux kernel to the hive and vice-versa, common components
   of network-based protocols like NFS.

So, to complete these goals, HiveFS would need to be:
1. A global filesystem available across multiple hosts.
2. A globally de-dupable filesystem, no attached to a single lun or filesystem mount point. Everything 
is technically one pool.
3. Can be shared to multiple hosts, but the hosts do not have to mount the tree at the root of the
filesystem. Storage can be virtualized in ways not currently possible, such as mounting sub-sets of a 
filesystem, joining filesystems to a larger tree, etc. The inode table is completely abstract in hiveFS.
4. Have ACL and expanded security support.
5. Have web-based media access support. IN addition to S3 support, this means that tags on a file could
potentially be stored in the hive.
6. The FS would be completely cloud-friendly, easily expanding across a hybrid cloud or multi-cloud setup.
7. Have built-in VMWare support. This would enable extended VMWare functionality. For instance, vmotion to
a remote site may be as easy as re-assigning an inode in the hive rather than copying data between sites if
the WAN link is fast enough to support cross-site storage (which for the hive is ethernet or Infiniband-based).

So, to this point what we've described is not so dissimilar to setting up a linux blade with NFS and then sharing the filesystem with several clients (our worker bees in this case) as a global filesystem. So, what does hiveFS buy that NFS doesn't? HiveFS is designed with these main architectural goals in mind:
1. Global deduplication. Arrays de-dupe generally on a volume level, or possibly on an aggregate or raid group level. HiveFS de-dupes across all storage under its management, and there is no real client limit. Even if multiple storage devices are setup behind the hive blad server(s), it will continue to de-dupe across all devices.
2. Made for Cloud Computing. Not only is the end goal to make it VMWare-aware but also cloud aware. Currently cloud storage is often more expensive than it seems at first and devoid of any real functionality at all. There are storage appliance images that can turn cloud storage into functional virtual arrays, but that adds a much more hefty price tag to the storage. HiveFS is designed to function as a co-lo appliance for cloud, but the central hive server can operate in the cloud, as a co-lo array, or at the Enterprise data center. It's designed so that cloud workloads can access storage from remote and it will look like any other storage. True to cloud concepts, however, it's management is much simpler than an array, more virtual in the sense it doesn't require disruptive re-zones, reboots, or other disruptive maintenance processes, and the client module (HiveFS Module and Hi_Command) can be embedded in a cloud image.
3. Unlike NFS, a directory structure of the admin's choosing is assigned to each client, and it can be re-configured on the fly without large copies or moves. Data trees can also be firewalled from specific clients, added to other clients, and grafted onto other trees instantly. Unlike other filesystems, the root directory is really at the central hive layer, and every client has a sub-directory as the first layer of management. That layer acts as a client's personal mount point. Meaning the client can only see the data placed in it's branch of the "tree" as though it had been hard linked, soft linked, or junctioned onto the path. To the central hive, however, those links can exist as actual directories to avoid linkig issues.
4. The Hive itself manages file copies. Although storage can be used underneath that supports replication, the design point of the hive software is to manage replication setup, backup copies, file versioning copies, and database testing cycle snapshots. Since it is globally de-dupable, methods need to exist to make sure the data is well-protected to ensure there is always more than one copy of every block. To the hive, both a backup, file version, or snapshot would all look like a snapshop copy that can be rolled backward or forward instantly like a CDM system. Like many filesystems today, files stored in several different paths would all have only one copy in the filesystem and links would exist that make the file look like it has many different copies without taking additional storage. The only physical additional copies would be the ones configured by replication, backups, or by file versioning (differences in blocks over a configurable number of versions of the file).
5. HiveFS acts on a protocol level on traditional ethernet, so it sees communications with storage on an inode and block level (all storage in the hive is initially block storage on the protocol level). Any block application can use the hive filesystem by default. However, the central hive blade server (or an additional blade server added to handle the extra load) can always share data from the hive on a file or object level (NFS, CIFS, or S3) to give access to mounts from other protocols, making hiveFS more verstile than a traditional file protocol like NFS or CIFS. To the hive, all the protocols are equal.
6. File versioning. A file can have a configurable number of copies as a backup method to go back to previous versions of theat file. Replication covers large-scale outages, so traditional backup is made unnecessary by the way these two functions behave. Further, since a directory structure can be modified on the fly, it would support the same live mount function as today's CDM-based software.