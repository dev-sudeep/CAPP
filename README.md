# CAPP - A cross-platform app bundle utility


## Supported operating systems:
- Windows
- MacOS

## Bundle hierarchy:
```
MyApp.capp/  
├── instructions.(Any standard extension)     : How to run/use the app  
├── myExecutable.exe / myExecutable           : Unix/Windows Executable  
├── platform.txt                              : Contains "win" (for windows) or "unix" (for MacOS) on the first line , on the second line, the executable name and on the third line, the name of the instructions file. 
└── Any Other Random Stuff                    : Because creativity is limitless
```

## How to bundle:
### METHOD 1: MANUAL BUNDLING
1. Keep all of the required files in a folder.
2. Zip the folder.
3. Rename the .zip extension to .capp
4. Distribute it as per your wish.

### METHOD 2: USING THE BUNDLER PROGRAM:
1. Keep all the required files in a folder (platform.txt will be created ny the bundler so that is unnecessary).
2. Run the bundler program (according to platform).
3. Give inputs.
4. The .capp file is created.


