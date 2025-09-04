from fastapi import APIRouter, Depends, HTTPException
from fastapi.responses import Response
from ..deps import get_registry
from ..templates.loader import TemplateRegistry

router = APIRouter(prefix="/templates", tags=["templates"])

@router.get("")
async def list_templates(registry: TemplateRegistry = Depends(get_registry)):
    filtered_templates = [
        {
            "template_id": template["template_id"],
            "template_name": template["template_name"],
            "template_desc": template["template_desc"],
            "template_thumbnail": template.get("template_thumbnail", ""),
        }
        for template in registry.templates
    ]
    return {"templates": filtered_templates}
