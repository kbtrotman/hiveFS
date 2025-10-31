from rest_framework import serializers
from .models import Todo

class HiveSerializer(serializers.ModelSerializer):
    class Meta:
        model = Hive
        fields = ('id', 'title', 'description', 'completed')
