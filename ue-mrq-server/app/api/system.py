from fastapi import APIRouter, HTTPException, Query
from fastapi.responses import FileResponse, HTMLResponse
from pydantic import BaseModel
from pathlib import Path
import sys
import subprocess
from ..config import settings
import os
from ..db.database import session_scope
from ..db.models import JobArtifact
from urllib.parse import quote, unquote
import html

router = APIRouter(prefix="/system", tags=["system"])


class OpenPathRequest(BaseModel):
    path: str


@router.post("/open-path")
async def open_path(req: OpenPathRequest):
    """
    Open a file or directory location on the server host.
    For safety, only paths under DATA_ROOT are allowed.
    """
    target = Path(req.path).resolve()

    if not _is_allowed_path(target):
        raise HTTPException(status_code=400, detail={"code": "PATH_NOT_ALLOWED"})

    # Choose directory to open
    open_dir = target if target.is_dir() else target.parent
    if not open_dir.exists():
        raise HTTPException(status_code=404, detail={"code": "PATH_NOT_FOUND"})

    try:
        if sys.platform.startswith("win"):
            if target.exists() and target.is_file():
                explorer_args = ["explorer", f"/select,\"{str(target)}\""]
            else:
                explorer_args = ["explorer", str(open_dir)]
            subprocess.Popen(explorer_args)
            focus_script = (
                "$sig = '[DllImport(\"user32.dll\")]public static extern bool SetForegroundWindow(IntPtr hWnd);';"
                "Add-Type -MemberDefinition $sig -Name Win32 -Namespace Native;"
                "Start-Sleep -Milliseconds 250;"
                "$shell = New-Object -ComObject Shell.Application;"
                "$window = $shell.Windows() | Where-Object { $_.FullName -like '*explorer*' } | Sort-Object HWND -Descending | Select-Object -First 1;"
                "if ($window) { [Native.Win32]::SetForegroundWindow([IntPtr]$window.HWND) | Out-Null }"
            )
            subprocess.Popen(["powershell", "-NoProfile", "-Command", focus_script])
        elif sys.platform == "darwin":
            # macOS: reveal file or open directory
            if target.exists() and target.is_file():
                subprocess.Popen(["open", "-R", str(target)])
            else:
                subprocess.Popen(["open", str(open_dir)])
        else:
            # Linux / others
            subprocess.Popen(["xdg-open", str(open_dir)])
    except Exception as e:
        raise HTTPException(status_code=500, detail={"code": "OPEN_FAILED", "message": str(e)})

    return {"status": "ok"}


def _is_allowed_path(target: Path) -> bool:
    base = Path(settings.DATA_ROOT).resolve()
    if base == target or base in target.parents:
        return True
    with session_scope() as db:
        tgt_norm = os.path.normcase(os.path.normpath(str(target)))
        for row in db.query(JobArtifact).all():
            vp = row.video_path
            if not vp:
                continue
            vp_norm = os.path.normcase(os.path.normpath(vp))
            if vp_norm == tgt_norm:
                return True
    return False


def _media_type_for(target: Path) -> str:
    ext = target.suffix.lower()
    if ext == ".mp4":
        return "video/mp4"
    if ext == ".mov":
        return "video/quicktime"
    if ext == ".webm":
        return "video/webm"
    if ext == ".mkv":
        return "video/x-matroska"
    return "application/octet-stream"


@router.get("/video")
async def serve_video(path: str = Query(..., description="Absolute path to video file")):
    raw_path = unquote(path)
    target = Path(raw_path).resolve()
    if not _is_allowed_path(target):
        raise HTTPException(status_code=400, detail={"code": "PATH_NOT_ALLOWED"})
    if not target.exists() or not target.is_file():
        raise HTTPException(status_code=404, detail={"code": "PATH_NOT_FOUND"})
    media = _media_type_for(target)
    return FileResponse(
        path=str(target),
        media_type=media,
        filename=target.name,
        content_disposition_type="inline",
    )


@router.get("/player", response_class=HTMLResponse)
async def video_player(path: str = Query(..., description="Absolute path to video file")):
    raw_path = unquote(path)
    target = Path(raw_path).resolve()
    if not _is_allowed_path(target):
        raise HTTPException(status_code=400, detail={"code": "PATH_NOT_ALLOWED"})
    if not target.exists() or not target.is_file():
        raise HTTPException(status_code=404, detail={"code": "PATH_NOT_FOUND"})

    media = _media_type_for(target)
    src = f"/system/video?path={quote(str(target))}"
    safe_name = html.escape(target.name)
    body = f"""
    <!doctype html>
    <html lang=\"zh-CN\">
      <head>
        <meta charset=\"utf-8\" />
        <title>{safe_name}</title>
        <style>
          body {{ margin: 0; background: #0b0f1a; color: #e2e8f0; font-family: sans-serif; display: flex; align-items: center; justify-content: center; min-height: 100vh; }}
          .wrap {{ width: min(90vw, 1200px); }}
          video {{ width: 100%; height: auto; border-radius: 12px; box-shadow: 0 20px 40px rgba(0,0,0,0.35); background: #000; }}
          h1 {{ font-size: 16px; font-weight: 500; margin-bottom: 12px; text-align: center; color: #94a3b8; }}
        </style>
      </head>
      <body>
        <div class=\"wrap\">
          <h1>{safe_name}</h1>
          <video controls autoplay>
            <source src=\"{src}\" type=\"{media}\" />
            您的浏览器不支持 HTML5 视频。
          </video>
        </div>
      </body>
    </html>
    """
    return HTMLResponse(content=body)
