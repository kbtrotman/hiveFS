from django.shortcuts import render

# Create your views here.
from rest_framework.response import Response
from rest_framework.viewsets import ReadOnlyModelViewSet, ViewSet

from django.utils.dateparse import parse_datetime
from rest_framework.generics import ListAPIView
from rest_framework.permissions import IsAuthenticated 

from .models import (
    DiskStatus,
    Storage_Nodes,
    Storage_Node_Stats,
    StorageNodeFsStats,
    StorageNodeDiskStats,
)
from .serializers import (
    DiskStatusSerializer,
    StorageNodeSerializer,
    StorageNodeStatSerializer,
    StorageNodeFsStatsSerializer,
    StorageNodeDiskStatsSerializer,
)
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


def _apply_common_filters(qs, request, ts_field: str):
    node_id = request.query_params.get("node_id")
    if node_id:
        try:
            qs = qs.filter(node_id=int(node_id))
        except ValueError:
            pass

    since = request.query_params.get("since")
    if since:
        dt = parse_datetime(since)
        if dt:
            qs = qs.filter(**{f"{ts_field}__gte": dt})

    until = request.query_params.get("until")
    if until:
        dt = parse_datetime(until)
        if dt:
            qs = qs.filter(**{f"{ts_field}__lte": dt})

    # Default: newest first
    qs = qs.order_by(f"-{ts_field}")

    # Simple safety cap
    limit = request.query_params.get("limit")
    if limit:
        try:
            limit_int = max(1, min(int(limit), 5000))
            qs = qs[:limit_int]
        except ValueError:
            pass
    else:
        qs = qs[:500]

    return qs


class StorageNodeFsStatsListView(ListAPIView):
    permission_classes = [IsAuthenticated]  # adjust
    serializer_class = StorageNodeFsStatsSerializer

    def get_queryset(self):
        qs = StorageNodeFsStats.objects.all()

        fs_path = self.request.query_params.get("fs_path")
        if fs_path:
            qs = qs.filter(fs_path=fs_path)

        fs_type = self.request.query_params.get("fs_type")
        if fs_type:
            qs = qs.filter(fs_type=fs_type)

        health = self.request.query_params.get("health")
        if health:
            qs = qs.filter(health=health)

        return _apply_common_filters(qs, self.request, ts_field="fs_ts")


class StorageNodeDiskStatsListView(ListAPIView):
    permission_classes = [IsAuthenticated]  # adjust
    serializer_class = StorageNodeDiskStatsSerializer

    def get_queryset(self):
        qs = StorageNodeDiskStats.objects.all()

        disk_name = self.request.query_params.get("disk_name")
        if disk_name:
            qs = qs.filter(disk_name=disk_name)

        fs_path = self.request.query_params.get("fs_path")
        if fs_path:
            qs = qs.filter(fs_path=fs_path)

        rotational = self.request.query_params.get("rotational")
        if rotational in ("0", "1"):
            qs = qs.filter(disk_rotational=(rotational == "1"))

        health = self.request.query_params.get("health")
        if health:
            qs = qs.filter(health=health)

        return _apply_common_filters(qs, self.request, ts_field="disk_ts")

class DiskStatusListView(ListAPIView):
    permission_classes = [IsAuthenticated]  # adjust
    serializer_class = DiskStatusSerializer

    def get_queryset(self):
        qs = DiskStatus.objects.all()

        node_id = self.request.query_params.get("node_id")
        if node_id:
            try:
                qs = qs.filter(node_id=int(node_id))
            except ValueError:
                pass

        disk_name = self.request.query_params.get("disk_name")
        if disk_name:
            qs = qs.filter(disk_name=disk_name)

        disk_serial = self.request.query_params.get("disk_serial")
        if disk_serial:
            qs = qs.filter(disk_serial=disk_serial)

        media_type = self.request.query_params.get("media_type")
        if media_type:
            qs = qs.filter(media_type=media_type)

        interface_type = self.request.query_params.get("interface_type")
        if interface_type:
            qs = qs.filter(interface_type=interface_type)

        smart_health = self.request.query_params.get("smart_health")
        if smart_health:
            qs = qs.filter(smart_health=smart_health)

        paged_out = self.request.query_params.get("paged_out")
        if paged_out is not None:
            normalized = paged_out.lower()
            if normalized in ("1", "true", "yes"):
                qs = qs.filter(paged_out=True)
            elif normalized in ("0", "false", "no"):
                qs = qs.filter(paged_out=False)

        return _apply_common_filters(qs, self.request, ts_field="updated_at")
