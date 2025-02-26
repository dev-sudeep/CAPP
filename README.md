## CAPP - A cross-platform app bundle utility


### Supported operating systems:
- Windows
- MacOS

### Bundle hierarchy:
```
MyApp.capp/  
├── instructions.(Any standard extension)     : How to run/use the app  
├── myExecutable.exe / myExecutable           : Unix/Windows Executable  
├── platform.txt                              : Contains "win" (for windows) or "unix" (for MacOS) on the first line , on the second line, the executable name. 
└── Any Other Random Stuff                    : Because creativity is limitless
```

### How to bundle
1. Keep all of the required files in a folder.
2. Zip the folder.
3. Rename the .zip extension to .capp
4. Distribute it as per your wish.

