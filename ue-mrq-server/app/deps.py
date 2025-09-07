# app/deps.py
from fastapi import Request
from .templates.loader import TemplateRegistry

def get_registry(request: Request) -> TemplateRegistry:
    return request.app.state.registry  # Injected by main.py during startup
