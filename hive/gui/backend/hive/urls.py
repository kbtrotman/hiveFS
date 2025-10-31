from django.contrib import admin
from django.urls import path, include
from rest_framework import routers
from hive import views

router = routers.DefaultRouter()
router.register(r'api', views.apiView, 'api')

urlpatterns = [
    path('admin/', admin.site.urls),
    path('api/', include(router.urls)),
]
