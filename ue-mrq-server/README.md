# UE MRQ Cloud (MVP)

```bash
# 1) 环境（Anaconda 推荐 3.10）
conda create -n ue-mrq python=3.10 -y
conda activate ue-mrq
pip install -r requirements.txt

# 2) 配置环境变量（或 .env）
set UE_ROOT=D:\\Install\\UE_5.4
set UPROJECT=D:\\Dev\\r0\\r0.uproject
set FFMPEG=D:\\Install\\ffmpeg\\bin\\ffmpeg.exe

# 3) 运行
uvicorn app.main:app --reload --port 8080

# API
GET  /templates
POST /jobs
GET  /jobs/{id}
POST /jobs/{id}/cancel