LiJy-launch
===========

Lightweight multiplatform Jython-launcher

Why write another Jython-launcher?
----------------------------------

The current situation is as follows:

On Linux the Jython-launcher is a hard to read and vastly unmaintainable sh-script, that is even said to be buggy.
On Windows it used to be a similar situation with a bat-script that was recently replaced by a python-script. This is currently provided as an exe-file via py2exe and thus bundles the CPython interpreter as a dependency.

Is that weird? Jython bundles CPython just as a launcher? Yes, but it was - however - the only feasible solution within given time-constraints. First do things right...

Another downside of this solution is that on Windows a process cannot replace itself by another process. That means that Jython always runs as a *subprocess* of CPython.


How it works
------------

This project is a sandbox for creating a new lightweight Jython launcher based on JNI (similar to java.exe). We will combine the sources of java.exe and CPython-launcher to produce a platform-independent launcher-source that will provide elegant and lightweight binary Jython launchers for Linux, Windows, OSX and other target platforms.


Current state
-------------

The launcher was not extensively tested yet, but supports almost all options and environment variables that jython.py supports. It is currently Linux-only, but a Windows-version is the next major goal. It has only been tested with Open JDK.


License
-------

As LiJy-launch inherits some code from the original Open JDK Java-launcher, it is released under the same license as Open JDK, which is GPL v2 with classpath exception. See the file LICENSE for details.


Contact
-------

For now write to stefan.richthofer@jyni.org.

