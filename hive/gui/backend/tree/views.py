from rest_framework.response import Response
from rest_framework.viewsets import ReadOnlyModelViewSet, ViewSet

from .models import UiHostMap, UiVirtualNode, UnifiedTree
from .serializers import (
    UiHostMapSerializer,
    UiVirtualNodeSerializer,
    UnifiedTreeSerializer,
)


class UiVirtualNodeViewSet(ReadOnlyModelViewSet):
    """Expose read-only access to UI virtual nodes defined in hive_api."""

    queryset = UiVirtualNode.objects.all()
    serializer_class = UiVirtualNodeSerializer


class UiHostMapViewSet(ReadOnlyModelViewSet):
    """Expose read-only access to host-to-root dentry mappings."""

    queryset = UiHostMap.objects.all()
    serializer_class = UiHostMapSerializer



class TreeNodeViewSet(ViewSet):
    """
    Gets the HiveFS Tree View by parent dir.

    Usage:
    GET /api/v1/tree                        -> top-level nodes (where parent is NULL)
    GET /api/v1/tree?parent=<node-id>       -> children for the node provided in <node-id>
    GET /api/v1/tree?filelist=<node-id>     -> same as parent, alternate param

    Response:
    [{ "key": "<node_id>", "title": "<name>", "isLeaf": bool, "hasChildren": bool }]
    """

    # permission_classes = [IsAuthenticated]

    def list(self, request):
        # Prefer ?parent=, but allow ?filelist= as an alias
        parent_key = (
            request.query_params.get("parent")
            or request.query_params.get("filelist")
            or "root"
        )

        # Decide which nodes to fetch
        if parent_key.lower() == "root":
            qs = UnifiedTree.objects.filter(parent_node_id__isnull=True)
        else:
            qs = UnifiedTree.objects.filter(parent_node_id=parent_key)

        qs = qs.order_by("name")

        payload = [
            {
                "key": node.node_id,
                "title": node.name,
                "isLeaf": not node.has_children,
                "hasChildren": bool(node.has_children),
                "nodeKind": node.node_kind,
                "inodeId": node.inode_id,
                "dentryId": node.dentry_id,
            }
            for node in qs
        ]
        return Response(payload)
