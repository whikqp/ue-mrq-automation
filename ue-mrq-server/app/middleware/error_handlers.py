from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi import HTTPException
from fastapi.responses import JSONResponse

def register_error_handlers(app: FastAPI):
    """
    Register minimal error handlers:
    - HTTPException: keep status/detail as is 
    - RequestValidationError: 422 with detailed validation errors
    - Exception: 500 with exception message
    """
    @app.exception_handler(HTTPException)
    async def http_exception_handler(request: Request, exc: HTTPException):
        # Preserve origional status and detail shape
        return JSONResponse(
            status_code=exc.status_code,
            content={"detail": exc.detail}
        )
    
    @app.exception_handler(RequestValidationError)
    async def validation_exception_handler(request: Request, exc: RequestValidationError):
        # Return detailed pydantic/validation errors
        return JSONResponse(
            status_code=422,
            content={"detail": exc.errors()}
        )
    
    @app.exception_handler(Exception)
    async def general_exception_handler(request: Request, exc: Exception):
        # Minimal 500 response with exception string
        return JSONResponse(
            status_code=500,
            content={"detail": str(exc)}
        )