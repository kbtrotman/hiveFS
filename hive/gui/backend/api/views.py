from rest_framework.response import Response
from rest_framework.viewsets import ViewSet
from rest_framework.views import APIView
from rest_framework.response import Response
from rest_framework import status as http_status
from .bootstrap import call_bootstrap, BootstrapError
from .newtoken import request_newtoken
class BootstrapStatusView(APIView):
    authentication_classes = []  # allow pre-login if desired
    permission_classes = []      # tighten later

    def get(self, request):
        try:
            r = call_bootstrap("status")
            return Response(r)
        except BootstrapError as e:
            return Response({"ok": False, "error": str(e)}, status=http_status.HTTP_502_BAD_GATEWAY)

class BootstrapInitView(APIView):
    authentication_classes = []
    permission_classes = []

    def post(self, request):
        try:
            r = call_bootstrap("cluster_init", request.data)
            return Response(r)
        except BootstrapError as e:
            return Response({"ok": False, "error": str(e)}, status=http_status.HTTP_502_BAD_GATEWAY)

class AddNodeView(APIView):
    '''
    Mutation to Add a Node
    To be called only from the correct interface and in the correct format.
    A published protocol is not available to do this remotely. 
    '''
    authentication_classes = []
    permission_classes = []

    def post(self, request):
        try:
            r = call_bootstrap("node_join", request.data)
            return Response(r)
        except BootstrapError as e:
            return Response({"ok": False, "error": str(e)}, status=http_status.HTTP_502_BAD_GATEWAY)

class AddForeignerView(APIView):
    authentication_classes = []
    permission_classes = []

    def post(self, request):
        try:
            r = call_bootstrap("add_foreigner", request.data)
            return Response(r)
        except BootstrapError as e:
            return Response({"ok": False, "error": str(e)}, status=http_status.HTTP_502_BAD_GATEWAY)

class NewTokenView(APIView):
    authentication_classes = []
    permission_classes = []
 
    def post(self, request):
        try:
            r = request_newtoken(request.data)
            return Response(r)
        except BootstrapError as e:
            return Response({"ok": False, "error": str(e)}, status=http_status.HTTP_502_BAD_GATEWAY)   
    