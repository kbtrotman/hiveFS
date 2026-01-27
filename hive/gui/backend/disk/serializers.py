from rest_framework import serializers
from .models import Storage_Nodes, Storage_Node_Stats, StorageNodeFsStats, StorageNodeDiskStats

class StorageNodeSerializer(serializers.ModelSerializer):
    class Meta:
        model = Storage_Nodes
        fields = [
            "node_id",
            "node_name", 
            "storage_capacity_bytes",
            "storage_used_bytes",
            "storage_reserved_bytes",
            "storage_overhead_bytes",
            "meta_capacity_bytes",
            "meta_used_bytes",
            "meta_reserved_bytes",
            "meta_overhead_bytes",
        ]

class StorageNodeStatSerializer(serializers.ModelSerializer):
    class Meta:
        model = Storage_Node_Stats
        fields = [
            "node_id",
            "s_ts",
            "cpu",
            "mem_used",
            "mem_avail",            
            "read_iops",
            "write_iops",
            "total_iops",
            "writes_mbps",
            "reads_mbps",
            "t_throughput",
            "c_net_in",
            "c_net_out",
            "s_net_in",
            "s_net_out",
            "meta_chan_ps",
            "avg_wr_latency",
            "avg_rd_latency",
            "sees_warning",
            "sees_error",
            "message",
            "cont1_isok",
            "cont2_isok",
            "cont1_message",
            "cont2_message",
            "clients",
            "lavg",
            ]

class StorageNodeFsStatsSerializer(serializers.ModelSerializer):
    class Meta:
        model = StorageNodeFsStats
        fields = [
            "node_id",
            "fs_ts",
            "fs_name",
            "fs_path",
            "fs_type",
            "fs_total_bytes",
            "fs_used_bytes",
            "fs_avail_bytes",
            "fs_used_pct",
            "in_total_bytes",
            "in_used_bytes",
            "in_avail_bytes",
            "in_used_pct",
            "health",
        ]

class StorageNodeDiskStatsSerializer(serializers.ModelSerializer):
    class Meta:
        model = StorageNodeDiskStats
        fields = [
            "node_id",
            "disk_ts",
            "disk_name",
            "disk_path",
            "disk_size_bytes",
            "disk_rotational",
            "reads_completed",
            "writes_completed",
            "read_bytes",
            "write_bytes",
            "io_in_progress",
            "io_ms",
            "fs_path",
            "health",
        ]
