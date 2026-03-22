# CAPP - A simple app bundle utility

## Bundle hierarchy:
```
MyApp.capp/
├── main file : Either direct source code, executable, etc. 
├── install.sh : A shell script to install the application.
├── instructions.(Any standard extension)     : How to run/use the app   
├── uninstall.sh : A shell script to uninstall the application.
└── Anything else
```

## How to bundle:
### METHOD 1: MANUAL BUNDLING
1. Keep all of the required files in a folder
2. Zip the folder as (App).capp

### METHOD 2: USING THE BUNDLER PROGRAM:
1. Keep all the required files in a folder (platform.txt will be created ny the bundler so that is unnecessary).
2. Run the bundler program (according to platform).
3. Give inputs.
4. The .capp file is created.


