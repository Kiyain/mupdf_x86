@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86

set "X86_DIR=F:\projects\mupdf-wrapper\sdk\x86"
set "DLL=%X86_DIR%\mupdf_wrapper.dll"
set "DEF=%X86_DIR%\mupdf_wrapper_x86.def"
set "LIB_OUT=%X86_DIR%\mupdf_wrapper_vc10_x86.lib"

echo Exporting symbols from x86 DLL...
dumpbin /EXPORTS "%DLL%" /NOLOGO > "%X86_DIR%\exports_x86.txt"
echo dumpbin result: %ERRORLEVEL%

echo Generating .def and lib...
python -c "import re; content=open(r'%X86_DIR%\exports_x86.txt').read(); lines=content.splitlines(); out=['LIBRARY mupdf_wrapper','EXPORTS']; [out.append('    '+m.group(1)) for line in lines for m in [re.match(r'\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\w+)',line)] if m]; open(r'%DEF%','w').write('\n'.join(out)+'\n'); print(len(out)-2,'exports written')"

lib.exe /DEF:"%DEF%" /MACHINE:X86 /OUT:"%LIB_OUT%" /NOLOGO
echo lib.exe result: %ERRORLEVEL%

echo.
echo Done: %LIB_OUT%
endlocal
