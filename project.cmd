@echo off
if defined RC_PYTHON (
  "%RC_PYTHON%" "%~dp0tools\project.py" %*
) else (
  py -3 "%~dp0tools\project.py" %*
)
exit /b %errorlevel%

