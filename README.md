# hiveFS
A Hive Mind Filesystem

The problem with describing what I'm trying to do here is there is no moniker to describe it. So, I'll try my best. It could be called a Globally De-duped Virtualized Filesystem (or Filesystem Protocol). There's probably a hundred other things it could be called, including crazy. But the idea is to have a small module on a linux system that writes a local superblock and then emulates a filesystem by using a protocol to copy higher-level remote objects to the local system. By higher-level, I mean the filesystem isn't limited on the lun-layer with hiveFS. The storage appliance hub would keep inodes, dirs, files, etc, and it would keep the local structure of each client. However, the actual objects in that structure could be managed across any number of hosts or hypervisors. In other words, de-dupe would be global. File trees could be moved around from host to host with ease, and new filesystems created instantly from remote. Only one copy of each file could be kept, and versioning across multiple hosts would be possible. There are two components to hiveFS.

First the server (Hub):
The [Hive Applicance] or hive software. This device is the hub that controls everything. It would be a blade host, similar to existing array controllers, attached to a huge blob of storage behind it, cpus, memory, etc. The hive will be an installable software image (ISO) built on that blade that helps the admin to automatically configure the hardware into a large pool of storage as one central hive. Once the storage is accessible, rather than serving luns, our hive has a unique purpose, which is to:
   1. Require certificates to prove the host/spoke is a valid hive member.
   2. A configured hive worker will then be given a confirmed spoke on the wheel, which initiates his filesystem at whatever place you mount it to.
      
      i. We could also say the admin is doing a mkfs here, because in the hive a mkfs linux command will add a host as a spoke on that wheel.

   3. Transfer encrypted filesystem structures to the foreign host, his inode table. and de-duplicated block data.
      
      i. I should add here that most hosts already have a large block of local storage, so a local cache is prabably a good idea here 
         to increase performance and is feasible using existing disk space.
 
   4. Store the remote worker's data as it comes in and as in any filesystem, add links to existing data instead of physical storage.
   5. And store n-number of copies of each block in it's global store. (Replicated/DR'd/ad-nauseum)

Remote Clients or spokes run the hiveFS software, which is a true filesystem (module in linux with a user-space communication layer called hi-command.) with a local superblock written to a physical disk that points to hive-owned inodes and other filesystem structures stored remotely. The hive blade can serve a small number of CIFS/NFS/S3 shares or an additional blade/blades can serve larger numbers of shares. So long as the network stays active, the array presents its storage. (The superblock is necessary because I'm not greedy here. I don't want to try and do away with everything that a filesystem is at the moment....my goals are already pretty drastic...you could even say far-fetched, so I'm sticking to what I know and am familiar with until it seems inplausible to continue in that vein.)

I'm very much not a windows developer, but the eventual goal is to support linux and windows, the primary O/S's for virtualization and the biggest footprint for O/S's in general. I wish to add support for VMWare API and S3 direct from the hub appliance via network/Infiniband. Support of legacy FC is not the goal for this project. Everything should run over IP.
