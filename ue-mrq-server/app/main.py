# app/main.py
from fastapi import FastAPI
from pathlib import Path
from .db.database import engine, Base
from .api import templates as templates_api, jobs as jobs_api
from .templates.loader import TemplateRegistry
from .scheduler.scheduler import Scheduler
from contextlib import asynccontextmanager
from .api import ue_notifications
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