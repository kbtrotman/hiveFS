from rest_framework.permissions import IsAuthenticated
from rest_framework.viewsets import ReadOnlyModelViewSet

from .models import UnifiedTree
from .serializers import UnifiedTreeSerializer
from .models import UiHostMap, UiVirtualNode
from .serializers import UiHostMapSerializer, UiVirtualNodeSerializer


class UiVirtualNodeViewSet(ReadOnlyModelViewSet):
    """Expose read-only access to UI virtual nodes defined in hive_api."""

    queryset = UiVirtualNode.objects.all()
    serializer_class = UiVirtualNodeSerializer


class UiHostMapViewSet(ReadOnlyModelViewSet):
    """Expose read-only access to host-to-root dentry mappings."""

    queryset = UiHostMap.objects.all()
    serializer_class = UiHostMapSerializer



class TreeNodeViewSet(viewsets.ViewSet):
    """
    GET /api/v1/tree?parent=<id>
    Returns array[{id, label, hasChildren}]
    """
    def list(self, request):
        parent = request.query_params.get("parent")
        # Fetch your nodes from DB here (pseudo-code):
        qs = TreeNode.objects.filter(parent__isnull=True) if not parent else TreeNode.objects.filter(parent_id=parent)
        payload = [
            {
                "id": str(n.id),
                "label": n.name,
                "hasChildren": n.children.exists(),   # or bool(n.child_count)
            }
            for n in qs.order_by("name")
        ]
        return Response(payload)