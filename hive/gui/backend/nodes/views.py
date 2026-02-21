from rest_framework.response import Response
from rest_framework.viewsets import ViewSet

from .models import HardwareStatus, Storage_Nodes, Storage_Node_Stats
from .serializers import HardwareStatusSerializer, StorageNodeSerializer, StorageNodeStatSerializer


class StorageNodeViewSet(ViewSet):
    """
    Gets the HiveFS Storage Node View (all nodes or by node_id).

    Usage:
    GET /api/v1/node                        -> top-level nodes (where input is NULL), return all nodes
    GET /api/v1/node?node_id=<node-id>      -> specific node for the node provided in <node-id>

    Response:
    [{ "key": "<node_id>", ""node_name", "node_address", "node_uid", "node_serial", "node_guard_port", 
            "node_data_port", "last_heartbeat", "hive_version", "fenced","last_maintenance",
            "date_added_to_cluster", "storage_capacity_bytes", "storage_used_bytes", "storage_reserved_bytes",
            "storage_overhead_bytes", }]
    """

    # permission_classes = [IsAuthenticated]
    def list(self, request):
        node_id = request.query_params.get("node_id")

        # Support abbreviated form /nodes?<node_id>
        if not node_id:
            raw_query = (request.META.get("QUERY_STRING") or "").strip()
            if raw_query:
                node_id = raw_query.split("&", 1)[0]

        if node_id:
            qs = Storage_Nodes.objects.filter(node_id=node_id)
        else:
            qs = Storage_Nodes.objects.all().order_by("node_name")

        payload = [
            {
                "key": node.node_id,
                "node": node.node_name,
                "node_address":  node.node_address, 
                "node_uid": node.node_uid,
                "node_serial": node.node_serial,
                "node_guard_port": node.node_guard_port,
                "node_data_port": node.node_data_port,
                "last_heartbeat": node.last_heartbeat,
                "hive_version": node.hive_version,
                "fenced": node.fenced,
                "last_maintenance": node.last_maintenance,
                "date_added_to_cluster": node.date_added_to_cluster,
                "storage_capacity_bytes": node.storage_capacity_bytes,
                "storage_used_bytes": node.storage_used_bytes,
                "storage_reserved_bytes": node.storage_reserved_bytes,
                "storage_overhead_bytes": node.storage_overhead_bytes,
            }
            for node in qs
        ]
        return Response(payload)

class StorageNodeStatViewSet(ViewSet):
    """
    Gets the HiveFS Storage Node Stats View (all nodes or by node_id).

    Usage:
    GET /api/v1/node/stats                       -> top-level nodes (where input is NULL), return all nodes
    GET /api/v1/node/stats?node_id=<node-id>      -> specific node for the node provided in <node-id>

    Response:
    [{ "key": "<node_id>", ""node_name", "node_address", "node_uid", "node_serial", "node_guard_port", 
            "node_data_port", "last_heartbeat", "hive_version", "fenced","last_maintenance",
            "date_added_to_cluster", "storage_capacity_bytes", "storage_used_bytes", "storage_reserved_bytes",
            "storage_overhead_bytes", }]
    """

    # permission_classes = [IsAuthenticated]
    def list(self, request):
        node_id = request.query_params.get("node_id")

        # Support abbreviated form /nodes?<node_id>
        if not node_id:
            raw_query = (request.META.get("QUERY_STRING") or "").strip()
            if raw_query:
                node_id = raw_query.split("&", 1)[0]

        if node_id:
            qs = Storage_Nodes.objects.filter(node_id=node_id)
        else:
            qs = Storage_Nodes.objects.all().order_by("node_name")

        payload = [
            {
                "key": stats.node_id,
                "s_ts": stats.s_ts,
                "cpu":  stats.cpu, 
                "read_iops": stats.read_iops,
                "write_iops": stats.write_iops,
                "total_iops": stats.total_iops,
                "t_throughput": stats.t_throughput,
                "c_net_in": stats.c_net_in,
                "c_net_out": stats.c_net_out,
                "s_net_in": stats.s_net_in,
                "s_net_out": stats.s_net_out,
                "avg_latency": stats.avg_latency,
            }
            for stats in qs
        ]
        return Response(payload)


class HardwareStatusViewSet(ViewSet):
    """
    Provides read-only hardware component state for nodes.
    Supports filtering by node_id, component_type, and component_slot.
    """

    def list(self, request):
        node_id = request.query_params.get("node_id")

        if not node_id:
            raw_query = (request.META.get("QUERY_STRING") or "").strip()
            if raw_query and "=" not in raw_query:
                node_id = raw_query.split("&", 1)[0]

        component_type = request.query_params.get("component_type")
        component_slot = request.query_params.get("component_slot")

        filters = {}
        if node_id:
            filters["node_id"] = node_id
        if component_type:
            filters["component_type"] = component_type
        if component_slot:
            filters["component_slot"] = component_slot

        qs = HardwareStatus.objects.filter(**filters).order_by("node_id", "component_type", "component_slot")
        serializer = HardwareStatusSerializer(qs, many=True)
        return Response(serializer.data)
