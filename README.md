# LibWinService

Based on the original example Windows Service via MinGW project, this library quickly lets the user develope a Windows Service on the fly, with integrated IPC for multi-process communication. This allows the developer to get a Desktop interactable application to communciate directly with the service.

This library is still a work in progress, and is not yet completed. Currently everything is in a test state, but does have a working interface.

Use at your own risk;



This uses MinGW 64bit compiler.
I personally prefer to compile 64bit applications as this is what all future operating systems will eventually be required to run; however, this project should (this has not yet been tested) still compile for 32bit operating systems.

## Build

Use the provided build script to build the test application which interfaces directly with the library. If you wish to build a static library, simply build the `src` directory, and link all the object files with the gcc `ar` command.