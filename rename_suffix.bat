@echo off
echo 正在批量修改 .h.txt 为 .h ...
for /r %%i in (*.h.txt) do (
    ren "%%i" "%%~ni.h"
)
echo 正在批量修改 .cpp.txt 为 .cpp ...
for /r %%i in (*.cpp.txt) do (
    ren "%%i" "%%~ni.cpp"
)
echo 批量修改完成！
pause