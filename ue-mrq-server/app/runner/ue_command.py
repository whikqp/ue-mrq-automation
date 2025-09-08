from pathlib import Path
from ..config import settings

UNREAL = Path(settings.UE_ROOT) / "Engine" / "Binaries" / ("Win64" if (Path(settings.UE_ROOT)/"Engine").exists() else "Linux")
UE_EDITOR_CMD = str(UNREAL / "UnrealEditor-Cmd.exe") if UNREAL.name == "Win64" else str(UNREAL / "UnrealEditor-Cmd")
UE_EXECUTOR_CLASS = settings.EXECUTOR_CLASS


def build_ue_cmd(
    job_id: str,
    log_path: Path,
    map_name: str | None = None,
    map_path: str | None = None,
    level_sequence: str | None = None,
    movie_quality: str | None = "MEDIUM",
    movie_format: str | None = "mp4",
    movie_pipeline_config: str | None = None,
    game_mode_class: str | None = None,
    ) -> list[str]:
    
    final_cmd_list = [
        UE_EDITOR_CMD,
        settings.UPROJECT,
    ]

    # Choose either map_path (preferred) or map_name and append optional game mode
    map_url: str | None = None
    if map_path:
        map_url = map_path
    elif map_name:
        map_url = map_name

    if map_url is not None:
        if game_mode_class:
            map_url = f"{map_url}?game={game_mode_class}"
        final_cmd_list.append(map_url)

    final_cmd_list.append("-game")

    if level_sequence is not None:
        final_cmd_list.append(f"-LevelSequence={level_sequence}")

    if UE_EXECUTOR_CLASS is not None:
        final_cmd_list.append(f"-MoviePipelineLocalExecutorClass={UE_EXECUTOR_CLASS}")
    
    if movie_quality is not None:
        final_cmd_list.append(f"-MovieQuality={movie_quality}")

    if movie_format is not None:
        final_cmd_list.append(f"-MovieFormat={movie_format}")

    final_cmd_list.extend(
        [
            f"-JobId={job_id}",
            "-RenderOffscreen", "-Unattended", "-NOSPLASH", "-NoLoadingScreen", "-notexturestreaming",
            "-stdout", 
            f"ABSLOG={log_path}"
        ]
    )


    return final_cmd_list
