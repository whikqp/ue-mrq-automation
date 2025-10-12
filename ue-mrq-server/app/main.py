# app/main.py
from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from fastapi.responses import RedirectResponse
from pathlib import Path
from .db.database import engine, Base
from .api import templates as templates_api, jobs as jobs_api
from .templates.loader import TemplateRegistry
from .scheduler.scheduler import Scheduler
from contextlib import asynccontextmanager
from .api import ue_notifications
from .api import system as system_api
from .middleware.error_handlers import *
from .utils.logging import setup_logging
from .utils.logging_tools import *

setup_logging()

Base.metadata.create_all(bind=engine)

@asynccontextmanager
async def custom_lifespan(app: FastAPI):
    # Startup
    info("custom_lifespan: Startup")
    registry = TemplateRegistry(Path("configs/templates.json"))
    registry.load()
    app.state.registry = registry

    scheduler = Scheduler(registry)
    app.state.scheduler = scheduler
    scheduler.start()

    yield

    # Shutdown
    info("custom_lifespan: Shutdown")
    scheduler = getattr(app.state, "scheduler", None)
    if scheduler:
        scheduler.stop()

app = FastAPI(title="UE MRQ Server", lifespan=custom_lifespan)

# Register minimal global error handlers
register_error_handlers(app)

app.include_router(templates_api.router)
app.include_router(jobs_api.router)
app.include_router(ue_notifications.router)
app.include_router(system_api.router)

# Mount static UI at /ui (same-origin)
UI_DIR = Path(__file__).resolve().parent / "static" / "ui"
app.mount("/ui", StaticFiles(directory=str(UI_DIR), html=True), name="ui")

@app.get("/", include_in_schema=False)
async def root_redirect():
    return RedirectResponse(url="/ui/")
