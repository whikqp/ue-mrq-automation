from __future__ import annotations
from datetime import datetime, timezone, timedelta

try:
    from zoneinfo import ZoneInfo
    CN_TZ = ZoneInfo("Asia/Shanghai")
except Exception:
    CN_TZ = timezone(timedelta(hours=8))

UTC = timezone.utc

def now_cn() -> datetime:
    return datetime.now(CN_TZ)

def to_cn_iso(dt: datetime | None) -> str | None:
    if dt is None:
        return None
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=UTC)
    return dt.astimezone(CN_TZ).isoformat()