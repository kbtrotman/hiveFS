# HiveFS

HiveFS is a distributed, secure, SSD-optimized cluster filesystem designed for large-scale, multi-tenant environments where:

- clients and storage are strictly separated
- metadata consistency is critical
- storage must scale independently of compute

HiveFS departs from traditional clustered filesystem designs by prioritizing plane separation, performance determinism, and operational simplicity, using clustering as a foundation for virtualization rather than as a scaling mechanism.

HiveFS is built from the ground up as a *shared-nothing* storage system with a **single global filesystem tree**, **strong cryptographic identity**, and a **flexible sharing model that goes beyond traditional cluster filesystems**. HiveFS is architected around modern SSD realities rather than legacy HDD assumptions. Instead of treating flash like a sector-rewritable disk, HiveFS **uses a key-value storage engine aligned with how SSDs manage block groups internally**, reducing write amplification and positioning for emerging native KV to SSD technologies.


> ⚠️ **Status:** HiveFS is under active development. The core architecture is largely implemented, but it is not yet production-ready. The GUI and extended features are in process currently.

---

## Architecture overview

**Clients and storage nodes are never the same**

HiveFS enforces a hard separation between **clients** and **storage nodes (SNs)**:

- A **storage node is never a client**
- A **client is never a storage node**
- The number of clients is limited only by connection load and number of SN nodes

Clients connect using:

- a **high-performance Linux kernel module** (Windows kernel driver planned)
- a user-space **security guard (“Hi-Command”)** responsible for authentication, authorization, and policy enforcement

This design allows client numbers to scale independently, while keeping the load of each independant.

---

**Shared-nothing storage nodes**

Each storage node:

- Operates independently
- Uses its own commodity SSD
- Adds capacity and bandwidth when added to the cluster
- Reads and writes may pass through any node.
- Cluster-wide metadata consistency is maintained via a Raft-based replicated log.

HiveFS uses **4+2 erasure coding** to provide:

- tolerance for node failures
- efficient storage utilization
- horizontal scalability

Data is stored in a **custom key-value store optimized for SSD workloads**, featuring:

- delayed deletion
- scheduled garbage collection
- reduced write amplification
- The capacity of the **storage** layer is different from capacity of the virtual **filesystems**, which is set via:
   -- User-Level (AD) quotas
   -- Group level quotas
   -- Three filesystem-size limiting quotas: soft, hard, and hard-stop

The design explicitly allows extremely large **storage** layer deployments in the **exabyte- and beyond-scale**.

---

**Security model**

Security is intrinsic to HiveFS.

- All communication supports 256-bit TLS
- Mutual TLS (mTLS) for node and client identity
- CA-rooted trust validation
- One-time cryptographic enrollment tokens

There is no implicit trust between components.
Every node and client must cryptographically prove identity.

---

**A single global filesystem tree — with flexibility**

Unlike many clustered filesystems that are strictly shared-only, HiveFS exposes a **single global filesystem tree** that supports:

- **Node-specific branches**
- **Shared branches**
- **Exported branches** (NFS, WebDAV, S3)

A dynamic junctioning system allows:
-Private paths to become shared
-Subtrees to appear at different logical locations
-Different clients to see different tree structures

This enables multi-tenant, hybrid private/shared storage without rigid structural constraints, which makes HiveFS suitable for:
- multi-tenant environments
- cross-team/cross-security region data sharing
- hybrid private/shared data layouts

---

**Deployment**

HiveFS is designed as a **quasi-appliance**:

- Core control services run directly on the host
- User-facing services (API/UI) run in containers
Deployable on physical hardware or cloud VMs

HiveFS does not require Kubernetes and is not dependent on external orchestration frameworks.

---

## Design philosophy (The "What the [blank] were you thinking?")

**HiveFS is written by a storage engineer, for storage engineers.**

The design is influenced by years of operating large, complex storage systems that required constant human intervention to remain healthy. HiveFS intentionally prioritizes **operational sanity** over exposing endless tuning knobs and seperates physical storage from the logical filesystems, eliminating all traditional layers between the hardware and virtual filesystems, thereby reducing the cognitive load of managing them.

This is the original philosophy used when this project was being first fleshed out:

**1. Deterministic Performance**

Through plane separation, SSD-optimized layout, and controlled metadata coordination, performance is treated as a first-class property of the system rather than a byproduct of scale.

**Storage as a Native Discipline**

HiveFS is designed around the realities of modern SSD storage rather than adapting legacy disk models The architecture begins at the storage layer and builds upward.

**5. Security as a Core Primitive**

Cryptography and identity validation are integrated into the control and data paths as foundational components.
Security is not layered on top of the filesystem; it is intrinsic to cluster membership, communication, and data integrity.

**6. Virtualization as Abstraction, Not Illusion**

HiveFS decouples logical filesystems from physical topology.
Through virtualization, storage domains can scale and evolve independently of hardware layout, reducing operational complexity while preserving performance guarantees.

**7. Block-Oriented Foundation**

HiveFS is block-native by design. Higher-level storage models and access patterns are supported as extensions, preserving efficiency and control at the lowest layer.

**8. Integrated Redundancy**

Redundancy, historical tracking, and durability mechanisms are embedded within the storage architecture itself.
By treating resilience as intrinsic to the data plane, HiveFS reduces dependence on external backup workflows while preserving data integrity RPO/RTO. Today, all data goes to HDD or SSD for backup, therefore storage **is** the backup, and by treating it more effectively, we reduce the need for traditional backup.

**9. Virtualization-First Architecture**

HiveFS prioritizes virtual and logical isolation of the filesystems. Virtual & physical node storage should exist on the same system and should be sharable to all and de-duplicated across all data sources, whatever their platform.

**10. Automation by Architecture**

HiveFS is internally automated by design. Operational workflows, lifecycle events, and cluster coordination are implemented as intrinsic system behaviors, minimizing external orchestration requirements and reducing administrative overhead.

*Example: proactive space management*

As a concrete example, the metadata and snapshot volume manager intentionally maintains **reserved emergency capacity**.
If a filesystem encounters a space-related error condition, HiveFS can:

- automatically expand the affected filesystem to prevent immediate downtime
- continue serving requests while operators are notified
- allow reclaimed space to be returned later once the condition is resolved

The goal is to prevent outages caused by recoverable conditions, without hiding the underlying complexities from operators.

---

### Designed to support experts, not replace them

HiveFS is designed to reduce cognitive load for experienced operators:

Automation is conservative and explainable

Behavior is observable

Failure modes are explicit

The goal is not to hide complexity, but to eliminate unnecessary fragility and routine firefighting.

---

## Screenshots & UI Overview

While HiveFS is still under active development, the following screenshots illustrate the direction of the system and how the filesystem, nodes, and metadata are exposed to operators.

> These screenshots represent an early but functional UI and are intended to demonstrate architectural intent rather than final visual design. Note that this is a section of current on-going development and most of these screens have already been consolidated into a much better view, but they show the features of the filesystem and are a good introduction. We will post updates shortly.

---

### Cluster Dashboard

![Cluster Dashboard](assets/Screenshot_20260123_192842.png)

The main dashboard provides a high-level view of cluster performance and health.

---

### Global Filesystem Layout

![Global Filesystem Layout](assets/Screenshot_20260123_193037.png)

This view shows the **single global filesystem tree**.

---

### File Versioning & Tags

![File Versioning and Tags](assets/Screenshot_20260123_193444.png)

HiveFS includes native file versioning and tagging:

- tracks block-level changes as files are modified
- retains multiple historical snapshots of file contents
- allows reverting files to earlier versions on demand

---

### Storage Node Health

![Node Health Dashboard](assets/Screenshot_20260123_193614.png)

The node dashboard shows per-node health and status, including:

---

## Project status

HiveFS is currently:

Under heavy development

Stabilizing identity and PKI systems

Refining storage, garbage collection, and metadata handling

If you are interested in distributed storage systems, filesystem design, or secure cluster architectures, the project may be worth following as it evolves.
