from rest_framework import serializers
from .models import Storage_Nodes, Storage_Node_Stats

class StorageNodeSerializer(serializers.ModelSerializer):
    class Meta:
        model = Storage_Nodes
        fields = [
            "node_id",
            "node_name", 
            "node_address", 
            "node_uid",
            "node_serial", 
            "node_guard_port", 
            "node_data_port"
            "last_heartbeat",
            "hive_version",
            "fenced",
            "last_maintenance",
            "date_added_to_cluster", 
            "storage_capacity_bytes",
            "storage_used_bytes",
            "storage_reserved_bytes",
            "storage_overhead_bytes",
        ]

class StorageNodeStatSerializer(serializers.ModelSerializer):
    class Meta:
        model = Storage_Node_Stats
        fields = [
            "node_id",
            "s_ts",
            "cpu",
            "read_iops",
            "write_iops",
            "total_iops",
            "t_throughput",
            "c_net_in",
            "c_net_out",
            "s_net_in",
            "s_net_out",
            "avg_latency",
            ]