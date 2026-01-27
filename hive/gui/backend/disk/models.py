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

class StorageNodeFsStats(models.Model):
    HEALTH_OK = "ok"
    HEALTH_WARN = "warn"
    HEALTH_CRIT = "crit"
    HEALTH_CHOICES = [
        (HEALTH_OK, "ok"),
        (HEALTH_WARN, "warn"),
        (HEALTH_CRIT, "crit"),
    ]

    node_id = models.PositiveIntegerField()
    fs_ts = models.DateTimeField()

    fs_name = models.CharField(max_length=255)
    fs_path = models.CharField(max_length=255)
    fs_type = models.CharField(max_length=64)

    fs_total_bytes = models.BigIntegerField()
    fs_used_bytes = models.BigIntegerField()
    fs_avail_bytes = models.BigIntegerField()
    fs_used_pct = models.DecimalField(max_digits=5, decimal_places=2)

    in_total_bytes = models.BigIntegerField()
    in_used_bytes = models.BigIntegerField()
    in_avail_bytes = models.BigIntegerField()
    in_used_pct = models.DecimalField(max_digits=5, decimal_places=2)

    health = models.CharField(max_length=4, choices=HEALTH_CHOICES, default=HEALTH_OK)

    class Meta:
        managed = False
        db_table = "v_sn_fs_stats"
        indexes = [
            models.Index(fields=["fs_ts"], name="idx_fs_ts"),
        ]
        # Django can’t represent the compound PK cleanly; table enforces it.
        # Leave unmanaged and query by filters.


class StorageNodeDiskStats(models.Model):
    """
    Hardware / physical-disk stats table.

    Assumes a schema like:
      node_id, disk_ts, disk_name, disk_path, disk_size_bytes, disk_rotational,
      reads_completed, writes_completed, read_bytes, write_bytes,
      io_in_progress, io_ms, fs_path, health
    """

    HEALTH_OK = "ok"
    HEALTH_WARN = "warn"
    HEALTH_CRIT = "crit"
    HEALTH_CHOICES = [
        (HEALTH_OK, "ok"),
        (HEALTH_WARN, "warn"),
        (HEALTH_CRIT, "crit"),
    ]

    node_id = models.PositiveIntegerField()
    disk_ts = models.DateTimeField()

    disk_name = models.CharField(max_length=64)
    disk_path = models.CharField(max_length=128)

    disk_size_bytes = models.BigIntegerField()
    disk_rotational = models.BooleanField()  # 0 SSD / 1 HDD

    reads_completed = models.BigIntegerField()
    writes_completed = models.BigIntegerField()
    read_bytes = models.BigIntegerField()
    write_bytes = models.BigIntegerField()

    io_in_progress = models.BigIntegerField()
    io_ms = models.BigIntegerField()

    fs_path = models.CharField(max_length=255, null=True, blank=True)
    health = models.CharField(max_length=4, choices=HEALTH_CHOICES, default=HEALTH_OK)

    class Meta:
        managed = False
        db_table = "v_sn_disk_stats"
        indexes = [
            models.Index(fields=["disk_ts"], name="idx_disk_ts"),
            models.Index(fields=["node_id", "disk_name"], name="idx_node_disk"),
        ]
