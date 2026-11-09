@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cl /nologo /EHsc /O1 /MD /D_CRT_SECURE_NO_WARNINGS /Fe:tools\patcomm_vt.exe tools\patcomm_vt.cpp ole32.lib oleaut32.lib
