from rest_framework import serializers

from .models import Alert, EmailTarget, Notification, Schedule


class ScheduleSerializer(serializers.ModelSerializer):
    class Meta:
        model = Schedule
        fields = "__all__"


class NotificationSerializer(serializers.ModelSerializer):
    class Meta:
        model = Notification
        fields = "__all__"


class AlertSerializer(serializers.ModelSerializer):
    class Meta:
        model = Alert
        fields = "__all__"


class EmailTargetSerializer(serializers.ModelSerializer):
    class Meta:
        model = EmailTarget
        fields = "__all__"
