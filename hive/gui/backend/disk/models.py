from django.db import models

# Create your models here.

class Storage_Nodes(models.Model):
    node_id = models.IntegerField(primary_key=True)
    node_name = models.CharField(max_length=100, null=True)
    storage_capacity_bytes = models.IntegerField(null=True)
    storage_used_bytes = models.IntegerField(null=True)
    storage_reserved_bytes = models.IntegerField(null=True)
    storage_overhead_bytes = models.IntegerField(null=True)
    meta_capacity_bytes = models.BigIntegerField(null=True)
    meta_used_bytes = models.BigIntegerField(null=True)
    meta_reserved_bytes = models.BigIntegerField(null=True)
    meta_overhead_bytes = models.BigIntegerField(null=True)
    
    
    class Meta:
        managed = False
        db_table = 'v_nodes'
        
class Storage_Node_Stats(models.Model):        
    node_id = models.IntegerField(primary_key=True)
    s_ts = models.DateTimeField()
    
    cpu = models.IntegerField(null=True)
    mem_used = models.IntegerField(null=True)
    mem_avail = models.IntegerField(null=True)
    
    read_iops = models.IntegerField(null=True)
    write_iops = models.IntegerField(null=True)
    total_iops = models.IntegerField(null=True)
    
    writes_mbps = models.IntegerField(null=True)
    reads_mbps = models.IntegerField(null=True)
    t_throughput = models.IntegerField(null=True)
 
    c_net_in = models.IntegerField(null=True)
    c_net_out = models.IntegerField(null=True)

    s_net_in = models.IntegerField(null=True)
    s_net_out = models.IntegerField(null=True)

    meta_chan_ps = models.IntegerField(null=True)

    avg_wr_latency  = models.IntegerField(null=True)
    avg_rd_latency  = models.IntegerField(null=True)

    sees_warning  = models.IntegerField(null=True)
    sees_error = models.IntegerField(null=True)
    message = models.CharField(max_length=200, null=True)

    cont1_isok  = models.IntegerField(null=True)
    cont2_isok  = models.IntegerField(null=True)
    cont1_message = models.CharField(max_length=100, null=True)
    cont2_message = models.CharField(max_length=100, null=True)

    clients  = models.IntegerField(null=True)
    lavg = models.IntegerField(null=True)

    class Meta:
        managed = False
        db_table = 'v_stats'
        