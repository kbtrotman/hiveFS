from django.utils.deprecation import MiddlewareMixin

class DisableCORSMiddlewareForAPIs(MiddlewareMixin):
    def process_response(self, request, response):
        response["Access-Control-Allow-Origin"] = "http://localhost:3000"
        response["Access-Control-Allow-Credentials"] = "true"
        response["Access-Control-Allow-Headers"] = "Authorization, Content-Type"
        response["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
        if request.method == "OPTIONS":
            response.status_code = 200
        return response