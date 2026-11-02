@echo off
chcp 65001 >nul
title DSplay Studio
cd /d "%~dp0"

echo ============================================================
echo   DSplay Studio - Windows
echo ============================================================
echo.

set "PY="
where py >nul 2>&1
if not errorlevel 1 (
    py -3 -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>&1
    if not errorlevel 1 set "PY=py -3"
)
if not defined PY (
    where python >nul 2>&1
    if not errorlevel 1 (
        python -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>&1
        if not errorlevel 1 set "PY=python"
    )
)
if not defined PY (
    echo [HATA] Python 3.8 veya daha yenisi bulunamadi.
    echo https://www.python.org/downloads/ adresinden Python'u kurun.
    echo Kurulumda "Add python.exe to PATH" kutusunu isaretleyin.
    echo.
    pause
    exit /b 1
)

echo Python: %PY%
echo Studio aciliyor... Bu pencereyi Studio'yu kullandiginiz surece acik tutun.
echo.
%PY% launcher.py
if errorlevel 1 (
    echo.
    echo [HATA] Studio beklenmedik sekilde kapandi. Yukaridaki mesaji
    echo veya studio_hata.txt dosyasini gonderin.
    echo.
    pause
)
