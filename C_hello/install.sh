cc C_hello/hello.c -o C_hello/Chello
if [ ! -d "$HOME/.capp/bin" ]; then
    mkdir -p "$HOME/.capp/bin"
fi
mv C_hello/Chello "$HOME/.capp/bin"
