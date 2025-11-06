from rest_framework import serializers

from .models import UiHostMap, UiVirtualNode, UnifiedTree


class UiVirtualNodeSerializer(serializers.ModelSerializer):
    class Meta:
        model = UiVirtualNode
        fields = [
            "id",
            "parent",
            "name",
            "node_kind",
            "target_type",
            "target_host",
            "target_dentry",
            "sort_order",
        ]
        read_only_fields = ["id"]


class UiHostMapSerializer(serializers.ModelSerializer):
    class Meta:
        model = UiHostMap
        fields = [
            "host_id",
            "root_dentry",
        ]
        

class UnifiedTreeSerializer(serializers.ModelSerializer):
    class Meta:
        model = UnifiedTree
        fields = [
            "node_id", 
            "parent_node_id", 
            "name", 
            "node_kind",
            "inode_id",
            "dentry_id",
            "has_children",
        ]
