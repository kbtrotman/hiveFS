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
        
class Storage_Node_Stats(models.Model):        
    node_id = models.IntegerField(primary_key=True)
    s_ts = models.DateTimeField()
    cpu = models.IntegerField(null=True)
    read_iops = models.IntegerField(null=True)
    write_iops = models.IntegerField(null=True)
    total_iops = models.IntegerField(null=True)
    t_throughput = models.IntegerField(null=True)
    c_net_in = models.IntegerField(null=True)
    c_net_out = models.IntegerField(null=True)
    s_net_in = models.IntegerField(null=True)
    s_net_out = models.IntegerField(null=True)
    avg_latency = models.IntegerField(null=True)

    class Meta:
        managed = False
        db_table = 'v_sn_stats'