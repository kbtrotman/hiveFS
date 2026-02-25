from django.db import models


class Settings(models.Model):
    settings_id = models.BigAutoField(primary_key=True)
    cluster_allow_gui_management = models.BooleanField(default=False)
    cluster_encrypt_inter_cluster = models.BooleanField(default=False)
    cluster_allow_cli_access = models.BooleanField(default=True)
    cluster_notification_forwarder = models.CharField(max_length=255, null=True, blank=True)
    cluster_pki_username = models.CharField(max_length=255, null=True, blank=True)
    cluster_rollup_initial_minutes = models.PositiveIntegerField(default=10)
    cluster_rollup_daily_minutes = models.PositiveIntegerField(default=120)
    cluster_rollup_weekly_days = models.PositiveIntegerField(default=3)
    fs_client_timeout_ms = models.PositiveIntegerField(default=30000)
    fs_encrypt_client_traffic = models.BooleanField(default=True)
    fs_enable_tags = models.BooleanField(default=True)
    fs_metadata_target = models.CharField(max_length=128, null=True, blank=True)
    fs_log_target = models.CharField(max_length=128, null=True, blank=True)
    fs_metadata_space_gib = models.PositiveIntegerField(default=0)
    fs_kv_space_gib = models.PositiveIntegerField(default=0)
    fs_log_space_gib = models.PositiveIntegerField(default=0)
    perm_users = models.JSONField(null=True, blank=True)
    perm_groups = models.JSONField(null=True, blank=True)
    perm_roles = models.JSONField(null=True, blank=True)
    net_client_interface = models.CharField(max_length=64, null=True, blank=True)
    net_client_auto_dhcp = models.BooleanField(default=True)
    net_client_ip = models.CharField(max_length=64, null=True, blank=True)
    net_client_netmask = models.CharField(max_length=64, null=True, blank=True)
    net_cluster_interface = models.CharField(max_length=64, null=True, blank=True)
    net_cluster_auto_dhcp = models.BooleanField(default=True)
    net_cluster_ip = models.CharField(max_length=64, null=True, blank=True)
    net_cluster_netmask = models.CharField(max_length=64, null=True, blank=True)
    net_domain_name = models.CharField(max_length=255, null=True, blank=True)
    net_dns_server = models.CharField(max_length=64, null=True, blank=True)
    net_dns_secondary = models.CharField(max_length=64, null=True, blank=True)
    net_time_config = models.TextField(null=True, blank=True)
    rebal_node_add_fill_ratio = models.FloatField(default=0.30)
    rebal_node_add_throttle_bytes = models.BigIntegerField(default=134217728)
    rebal_capacity_high_water_ratio = models.FloatField(default=0.85)
    rebal_capacity_low_water_ratio = models.FloatField(default=0.65)
    rebal_capacity_skew_gap_ratio = models.FloatField(default=0.20)
    rebal_capacity_throttle_bytes = models.BigIntegerField(default=100663296)
    rebal_hotspot_read_threshold = models.PositiveIntegerField(default=4096)
    rebal_hotspot_throttle_bytes = models.BigIntegerField(default=67108864)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        db_table = "settings"

    def __str__(self):
        return f"Settings {self.settings_id}"
