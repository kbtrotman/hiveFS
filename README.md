# hiveFS
A Hive Mind Filesystem

The problem with describing what I'm trying to do here is similar to trying to describe a helicopter to someone who's never seen an airplane or copter. It's new, and not much like anything currently available. It's de-dupe, but that's a small part of it. It's a global filesystem, but it's not meant to be a GF, it's just one as a result of the final goal. There is no moniker to describe it. So, I'll try my best.

Virtualization is huge, and everything is virtualized, even storage to a degree. But is storage really virtualized? We can turn a physical lun into a "virtual lun" but what have we really done to virtualize it beyond just changing how its presented? A hive mind is a full virtualization of the underlying storage, far beyond anything else that's existed. It extends de-dupe from a single filesystem or device across as many hosts as you need/want. What I'm "developing" is hiveFS, but even hiveFS is just a driver to enable the true function here, which is a virtual storage appliance built on either open source software or Enterprise-grade software/hardware. A single appliance that can serve storage to any number of hosts, physical or virtual, limited only by how many disks you can put behind it. The only thing that will be stored locally for any machine is the filesystem superblock. The hiveFS software will then point to a remote hive that stores everyone's data using the hive version of the hiveFS software.

Though I'm very much not a windows developer, the eventual goal here is to support both linux and windows, the primary O/S's for virtualization and the biggest footprint for O/S's in general today.
