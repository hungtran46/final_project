@echo off
:: CMD wrapper for build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
