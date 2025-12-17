from rest_framework import serializers
from .models import Storage_Nodes, Storage_Node_Stats

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
