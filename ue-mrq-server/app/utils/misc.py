import glob
import os
from pathlib import Path
from urllib.parse import urlparse
from typing import Optional

def find_video_file(directory: str) -> Optional[str]:
    patterns = [
        os.path.join(directory, "*.mp4"),
        os.path.join(directory, "*.mov")
    ]

    for pattern in patterns:
        files = glob.glob(pattern)
        if files:
            normalized_path = Path(files[0]).as_posix()
            return normalized_path
        
    return None

def is_valid_url(url: Optional[str]) -> bool:
    if not url:
        return False
    
    try:
        parsed = urlparse(url)
        return all([parsed.scheme, parsed.netloc])
    except:
        return False