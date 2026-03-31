cc C_hello/hello.c -o C_hello/Chello
if [ ! -d "$HOME/bin" ]; then
    mkdir -p "$HOME/bin"
fi
mv C_hello/Chello $HOME/bin
