from __future__ import annotations
from pathlib import Path
from typing import Optional
import asyncio
import mimetypes
from urllib.parse import urlparse, unquote
try:
    import oss2 # type: ignore
except Exception:
    oss2 = None

import requests
from ..config import settings

def oss_filekey(url: str) -> str:
    """
    1. Extract the file key from an OSS URL(HTTP/HTTPS/OSS).
    2. If is already a file key, return it directly.
    3. Return None if invalid.
    """
    if url is None:
        return None
    
    url = url.strip()
    if not url:
        return url
    
    p = urlparse(url)

    # case 1: http/https/oss url
    if p.scheme in {"http", "https", "oss"} or p.netloc:
        path = p.path or ""
        return unquote(path.lstrip('/'))
    
    # case 2: already a file key, remove leading slash if any
    return unquote(url.lstrip('/'))

def _put_file_via_presigned_url(url: str, file_path: Path, content_type: Optional[str]) -> None:
    headers = {}
    if content_type:
        headers['Content-Type'] = content_type
    with open(file_path, 'rb') as f:
        response = requests.put(url, data=f, headers=headers, timeout=600)
        if not (200 <= response.status_code < 300):
            raise RuntimeError(f"OSS upload failed: status_code={response.status_code}, response={response.text[:512]}")
        
async def upload_via_presigned_url(url: str, file_path: str | Path, content_type: Optional[str] = None) -> None:
    """
    Upload a local file to OSS via a presigned URL. Coroutine, can be awaited.

    """
    p = Path(file_path)
    if not p.exists() or not p.is_file():
        raise FileNotFoundError(f"File not found: {file_path}")

    ct = content_type
    if ct is None:
        guessed, _ = mimetypes.guess_type(p.as_posix())
        ct = guessed or 'application/octet-stream'

    await asyncio.to_thread(_put_file_via_presigned_url, url, p, ct)        