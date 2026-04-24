cc C_hello\hello.c -o C_hello\Chello.exe
if not exist "%USERPROFILE%\.capp\bin" mkdir "%USERPROFILE%\.capp\bin"
move C_hello\Chello.exe "%USERPROFILE%\.capp\bin"
