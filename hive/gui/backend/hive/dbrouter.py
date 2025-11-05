from django.core.exceptions import PermissionDenied


class HiveRouter:
    """
    Route ORM operations across the API, metadata, and data databases.

    The API (default) and metadata databases remain writable, while the data
    database is treated as read-only.
    """

    default_db = "default"
    app_label_map = {
        "hive_meta": "meta",
        "hive_data": "data",
    }
    read_only_dbs = {"data"}

    def _app_label_for(self, model=None, hints=None):
        """Infer the app label from a model or the router hints."""
        hints = hints or {}
        if model is not None:
            return model._meta.app_label
        instance = hints.get("instance")
        if instance is not None:
            return instance._meta.app_label
        return hints.get("app_label")

    def _db_for_app_label(self, app_label):
        if app_label is None:
            return self.default_db
        return self.app_label_map.get(app_label, self.default_db)

    def db_for_read(self, model, **hints):
        app_label = self._app_label_for(model, hints)
        return self._db_for_app_label(app_label)

    def db_for_write(self, model, **hints):
        app_label = self._app_label_for(model, hints)
        target_db = self._db_for_app_label(app_label)
        if target_db in self.read_only_dbs:
            raise PermissionDenied("Write operations to hive_data are not allowed.")
        return target_db

    def allow_relation(self, obj1, obj2, **hints):
        db1 = self._db_for_app_label(obj1._meta.app_label)
        db2 = self._db_for_app_label(obj2._meta.app_label)
        if db1 and db2:
            return db1 == db2
        return None

    def allow_migrate(self, db, app_label, model_name=None, **hints):
        if db in self.read_only_dbs or app_label == "hive_data":
            return False
        target_db = self._db_for_app_label(app_label)
        if target_db:
            return target_db == db
        # Default to running unmapped apps (auth, admin, etc.) on the default DB.
        return db == self.default_db
