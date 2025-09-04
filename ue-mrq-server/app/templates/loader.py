import json
from pathlib import Path
from typing import Any, Dict, Tuple, Optional

_EXT_MIME = {
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".png": "image/png",
    ".webp": "image/webp",
}

class TemplateRegistry:
    def __init__(self, path: Path):
        self.path = path
        self._loaded: dict[str, Any] = {}

    def load(self) -> None:
        with open(self.path, 'r', encoding='utf-8') as f:
            data = json.load(f)

        items = data.get("templates", [])
        self._loaded = {it['template_id']: it for it in items}

    @property
    def templates(self) -> list[dict]:
        return list(self._loaded.values())

    def get(self, template_id: str) -> dict | None:
        return self._loaded.get(template_id)