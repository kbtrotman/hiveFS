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


class TreeNodeViewSet(ReadOnlyModelViewSet):
    queryset = UnifiedTree.objects.all()
    serializer_class = UnifiedTreeSerializer
    permission_classes = [IsAuthenticated]
