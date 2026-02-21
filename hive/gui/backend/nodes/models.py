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


class HardwareStatus(models.Model):
    COMPONENT_CPU = "cpu"
    COMPONENT_MEMORY = "memory"
    COMPONENT_NIC = "nic"
    COMPONENT_PSU = "psu"
    COMPONENT_FAN = "fan"
    COMPONENT_CONTROLLER = "controller"
    COMPONENT_DISK = "disk"
    COMPONENT_SENSOR = "sensor"
    COMPONENT_BACKPLANE = "backplane"
    COMPONENT_CHASSIS = "chassis"
    COMPONENT_OTHER = "other"
    COMPONENT_CHOICES = [
        (COMPONENT_CPU, "cpu"),
        (COMPONENT_MEMORY, "memory"),
        (COMPONENT_NIC, "nic"),
        (COMPONENT_PSU, "psu"),
        (COMPONENT_FAN, "fan"),
        (COMPONENT_CONTROLLER, "controller"),
        (COMPONENT_DISK, "disk"),
        (COMPONENT_SENSOR, "sensor"),
        (COMPONENT_BACKPLANE, "backplane"),
        (COMPONENT_CHASSIS, "chassis"),
        (COMPONENT_OTHER, "other"),
    ]

    HEALTH_OK = "ok"
    HEALTH_WARN = "warn"
    HEALTH_CRIT = "crit"
    HEALTH_OFFLINE = "offline"
    HEALTH_UNKNOWN = "unknown"
    HEALTH_CHOICES = [
        (HEALTH_OK, "ok"),
        (HEALTH_WARN, "warn"),
        (HEALTH_CRIT, "crit"),
        (HEALTH_OFFLINE, "offline"),
        (HEALTH_UNKNOWN, "unknown"),
    ]

    node_id = models.PositiveIntegerField(primary_key=True)
    component_type = models.CharField(max_length=16, choices=COMPONENT_CHOICES, default=COMPONENT_OTHER)
    component_slot = models.CharField(max_length=64)
    component_serial = models.CharField(max_length=128, null=True, blank=True)
    component_vendor = models.CharField(max_length=64, null=True, blank=True)
    component_model = models.CharField(max_length=128, null=True, blank=True)
    firmware_version = models.CharField(max_length=64, null=True, blank=True)
    health_state = models.CharField(max_length=16, choices=HEALTH_CHOICES, default=HEALTH_OK)
    health_reason = models.CharField(max_length=255, null=True, blank=True)
    status_flags = models.CharField(max_length=255, blank=True)
    temperature_c = models.DecimalField(max_digits=5, decimal_places=2, null=True, blank=True)
    voltage_v = models.DecimalField(max_digits=6, decimal_places=3, null=True, blank=True)
    current_a = models.DecimalField(max_digits=6, decimal_places=3, null=True, blank=True)
    power_w = models.DecimalField(max_digits=7, decimal_places=3, null=True, blank=True)
    fan_rpm = models.PositiveIntegerField(null=True, blank=True)
    error_count = models.PositiveIntegerField(default=0)
    last_error_ts = models.DateTimeField(null=True, blank=True)
    telemetry_json = models.JSONField(null=True, blank=True)
    paged_down = models.BooleanField(default=False)
    paged_down_ts = models.DateTimeField(null=True, blank=True)
    first_seen_ts = models.DateTimeField()
    last_seen_ts = models.DateTimeField()

    class Meta:
        managed = False
        db_table = 'v_hardware_status'
        unique_together = (("node_id", "component_type", "component_slot"),)
        indexes = [
            models.Index(fields=["node_id", "component_type"], name="idx_hw_node_component"),
            models.Index(fields=["component_serial"], name="idx_hw_component_serial"),
        ]
