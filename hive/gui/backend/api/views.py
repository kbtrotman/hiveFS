from rest_framework.response import Response
from rest_framework.viewsets import ViewSet
from rest_framework.response import Response
from rest_framework import status as http_status
from .bootstrap import call_bootstrap, BootstrapError
class BootstrapStatusView(ViewSet):
    authentication_classes = []  # allow pre-login if desired
    permission_classes = []      # tighten later

    def get(self, request):
        try:
            r = call_bootstrap("status")
            return Response(r)
        except BootstrapError as e:
            return Response({"ok": False, "error": str(e)}, status=http_status.HTTP_502_BAD_GATEWAY)

class BootstrapInitView(ViewSet):
    authentication_classes = []
    permission_classes = []

    def post(self, request):
        try:
            r = call_bootstrap("cluster_config", request.data)
            return Response(r)
        except BootstrapError as e:
            return Response({"ok": False, "error": str(e)}, status=http_status.HTTP_502_BAD_GATEWAY)

class AddNodeView(ViewSet):
    authentication_classes = []
    permission_classes = []

    def post(self, request):
        try:
            r = call_bootstrap("node_join", request.data)
            return Response(r)
        except BootstrapError as e:
            return Response({"ok": False, "error": str(e)}, status=http_status.HTTP_502_BAD_GATEWAY)
