@echo off

if EXIST logs goto clean_logs
md logs

:start-httpd
node httpd.js
goto done

:clean_logs
del /q logs\*.*
goto start-httpd

:done
