# LiJy-launch
Lightweight multiplatform Jython-launcher
=========================================

Why write another Jython-launcher?
==================================

The current situation is as follows:
On linux the Jython-launcher is a hard to read and vastly unmaintainable sh-script, that is even said to be buggy.
On windows it used to be a similar situation with a bat-script that was recently replaced by a python-script. This is currently provided as an exe-file via py2exe and thus bundles the CPython interpreter as a dependency.

Is that weird? Jython bundles CPython just as a launcher? Yes, but it was - however - the only feasible solution within given time-constraints. First do things right...
Another downside of this solution is that on Windows a process cannot replace itself by another process. That means that Jython always runs as a *subprocess* of CPython.


How it works
============

This project is a sandbox for creating a new lightweight Jython launcher based on JNI (similar to java.exe). We will combine the sources of java.exe and CPython-launcher to produce a platform-independent launcher-source that will provide elegant and lightweight binary Jython launchers for Linux, Windows, OSX and other target platforms.
