# app/deps.py
from fastapi import Request
from .templates.loader import TemplateRegistry

def get_registry(request: Request) -> TemplateRegistry:
    return request.app.state.registry  # 由 main.py 在 startup 时注入
