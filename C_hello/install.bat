cc C_hello\hello.c -o C_hello\Chello.exe
if not exist "%USERPROFILE%\bin" mkdir "%USERPROFILE%\bin"
move C_hello\Chello.exe "%USERPROFILE%\bin"
