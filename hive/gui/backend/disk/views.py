from django.shortcuts import render

# Create your views here.
from rest_framework.response import Response
from rest_framework.viewsets import ReadOnlyModelViewSet, ViewSet

from .models import Storage_Nodes, Storage_Node_Stats
from .serializers import StorageNodeSerializer, StorageNodeStatSerializer


class DiskNodeViewSet(ViewSet):
    """
    Gets the HiveFS Disk Capacities View (all nodes or by node_id).

    Usage:
    GET /api/v1/node                        -> top-level nodes (where input is NULL), return all nodes
    GET /api/v1/node?node_id=<node-id>      -> specific node for the node provided in <node-id>

    Response:
    [{ "key": "<node_id>", "node_name", "storage_capacity_bytes", "storage_used_bytes", "storage_reserved_bytes",
            "storage_overhead_bytes", "meta_capacity_bytes", "meta_used_bytes", "meta_reserved_bytes",
            "meta_overhead_bytes", }]
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
                "storage_capacity_bytes": node.storage_capacity_bytes,
                "storage_used_bytes": node.storage_used_bytes,
                "storage_reserved_bytes": node.storage_reserved_bytes,
                "storage_overhead_bytes": node.storage_overhead_bytes,
                "meta_capacity_bytes": node.meta_capacity_bytes,
                "meta_used_bytes": node.meta_used_bytes,
                "meta_reserved_bytes": node.meta_reserved_bytes,
                "meta_overhead_bytes": node.meta_overhead_bytes,
            }
            for node in qs
        ]
        return Response(payload)

class DiskNodeStatViewSet(ViewSet):
    """
    Gets the HiveFS Disk Stats View (all nodes or by node_id).

    Usage:
    GET /api/v1/node/stats                       -> top-level nodes (where input is NULL), return all nodes
    GET /api/v1/node/stats?node_id=<node-id>      -> specific node for the node provided in <node-id>

    Response:
    [{ "key": "<node_id>", "s_ts", "cpu", "mem_used", "mem_avail", "read_iops", "write_iops", "total_iops",
            "writes_mbps", "reads_mbps", "t_throughput", "c_net_in", "c_net_out", "s_net_in", "s_net_out",
            "meta_chan_ps", "avg_wr_latency", "avg_rd_latency", "sees_warning", "sees_error", "message",
            "cont1_isok", "cont2_isok", "cont1_message", "cont2_message", "clients", "lavg", }]
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
            qs = Storage_Node_Stats.objects.filter(node_id=node_id)
        else:
            qs = Storage_Node_Stats.objects.all().order_by("s_ts")

        payload = [
            {
                "key": stats.node_id,
                "s_ts": stats.s_ts,
                "cpu": stats.cpu,
                "mem_used": stats.mem_used,
                "mem_avail": stats.mem_avail,                   
                "writes_mbps": stats.writes_mbps,
                "reads_mbps": stats.reads_mbps,
                "read_iops": stats.read_iops,
                "write_iops": stats.write_iops,
                "total_iops": stats.total_iops,
                "t_throughput": stats.t_throughput,
                "c_net_in": stats.c_net_in,
                "c_net_out": stats.c_net_out,
                "s_net_in": stats.s_net_in,
                "s_net_out": stats.s_net_out,  
                "meta_chan_ps": stats.meta_chan_ps,
                "avg_wr_latency": stats.avg_wr_latency,
                "avg_wr_latency": stats.avg_wr_latency,
                "sees_warning": stats.sees_warning,
                "sees_error": stats.sees_error,
                "message": stats.message,
                "cont1_isok": stats.cont1_isok,
                "cont2_isok": stats.cont2_isok,
                "cont1_message": stats.cont1_message,
                "cont2_message": stats.cont2_message,
                "clients": stats.clients,
                "lavg": stats.lavg,
            }
            for stats in qs
        ]
        return Response(payload)
    