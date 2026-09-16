@echo off
rem compile.cmd -- ArcaOS build script for xbill-2.1 SDL2 port

set LOGFILE=compile.cmd.log

echo Starting xbill-2.1 build | tee %LOGFILE%
echo %DATE% %TIME% | tee -a %LOGFILE%

set EMXOMFLD_TYPE=WLINK
set EMXOMFLD_LINKER=wl.exe
set EMXOMFLD_PRELINK=0

make -f makefile.gcc 2>&1 | tee -a %LOGFILE%

if exist bin\xbill.exe (
    echo BUILD OK | tee -a %LOGFILE%
) else (
    echo BUILD FAILED | tee -a %LOGFILE%
)
