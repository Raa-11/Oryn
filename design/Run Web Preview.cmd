@echo off
if not exist "%~dp0preview\index.html" (
  echo File preview tidak ditemukan.
  pause
  exit /b 1
)
start "" "%~dp0preview\index.html"
