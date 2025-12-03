from django.db import models

# Create your models here.
class Storage_Nodes(models.Model):
    node_id = models.IntegerField(primary_key=True)
    node_name = models.CharField(max_length=100, null=True)
    node_address = models.CharField(max_length=64)
    node_uid = models.CharField(max_length=128)
    node_serial = models.CharField(max_length=100, null=True)
    node_guard_port = models.IntegerField(null=True)
    node_data_port = models.IntegerField(null=True)
    last_heartbeat = models.DateTimeField()
    hive_version = models.CharField(max_length=10)
    fenced = models.IntegerField(null=True)
    last_maintenance = models.DateTimeField()
    date_added_to_cluster = models.DateTimeField()
    storage_capacity_bytes = models.IntegerField(null=True)
    storage_used_bytes = models.IntegerField(null=True)
    storage_reserved_bytes = models.IntegerField(null=True)
    storage_overhead_bytes = models.IntegerField(null=True)
    
    class Meta:
        managed = False
        db_table = 'v_nodes'