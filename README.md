# Boxless: Headless DOSBox

This is a work-in-progress attempt to get a headless version of DOSBox running on Windows. The core functionality of video output and keyboard input are working, but there is no mouse, wave, MIDI,  or joystick support.

'Headless' means that the emulator doesn't display a window itself, but instead includes a server that allows client processes to connect and control the emulated machine. Many virtual machine managers support headless mode, but emulators (like DOSBox) rarely do.

In this case, the server allows clients (on the same physical machine only) to receive video from the emulated machine via a pipe and send keystrokes to it via a mailslot. 

## Building (Important)
Only the Visual Studio toolchain has been modified in this repository. The UNIX makefile toolchain is still the original files, so it will not build everything. Use the Visual Studio one.

No attempt has been made at cross-platform compatibility, this system relies on the Windows API extensively.