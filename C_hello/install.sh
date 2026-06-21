cc hello.c -o Chello
if [ ! -d "$HOME/.capp/bin" ]; then
    mkdir -p "$HOME/.capp/bin"
fi
mv Chello "$HOME/.capp/bin"
