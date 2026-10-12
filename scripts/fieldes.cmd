@echo off
rem fieldes.cmd: runs FielDes.exe from a command line, so that a batch file or a build script has its output and its exit code:
rem     fieldes run bracket_maker.fieldesapp --Wall_thickness 2.5
rem     fieldes export tool.py -o tool.fieldesapp
rem     fieldes info tool.fieldesapp
rem (cmd.exe waits for a program that is run from a batch file, which it does not do for one typed at its prompt.)
"%~dp0FielDes.exe" %*
exit /b %ERRORLEVEL%
