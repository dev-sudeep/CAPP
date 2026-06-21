gcc hello.c -o Chello.exe
if not exist "%USERPROFILE%\.capp\bin" mkdir "%USERPROFILE%\.capp\bin"
move Chello.exe "%USERPROFILE%\.capp\bin"
