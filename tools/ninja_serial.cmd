@echo off
if defined IDF_PYTHON_ENV_PATH (
  "%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%~dp0build_all_sequential.py" %*
) else (
  python "%~dp0build_all_sequential.py" %*
)
