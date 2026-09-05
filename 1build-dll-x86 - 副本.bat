@echo off
setlocal

echo ========================================
echo MuPDF Wrapper - Build x86 DLL
echo ========================================
echo.

set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat"
set "MUPDF_DIR=F:\projects\mupdf-wrapper"

cd /d "%MUPDF_DIR%"

echo [1/2] Configuring environment (x86)...
call "%VCVARS%" x86 >nul 2>&1

echo [2/2] Compiling DLL (x86)...
cl /EHsc /MD /utf-8 /W3 /DMUPDF_WRAPPER_EXPORTS ^
   /I"%MUPDF_DIR%\include" ^
   /I"%MUPDF_DIR%\include\mupdf" ^
   /I"%MUPDF_DIR%\src" ^
   /Fe"%MUPDF_DIR%\sdk\x86\mupdf_wrapper.dll" ^
   /LD ^
   "%MUPDF_DIR%\src\mupdf_wrapper.cpp" ^
   "%MUPDF_DIR%\src\print_engine.cpp" ^
   "%MUPDF_DIR%\lib\x86\libmupdf.lib" ^
   kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib

if %ERRORLEVEL% neq 0 (
    echo.
    echo ========== DLL Build Failed ==========
    endlocal
    exit /b 1
)

echo.
echo ========== x86 DLL Build Succeeded! ==========
echo Output: %MUPDF_DIR%\sdk\x86\mupdf_wrapper.dll
echo Library: %MUPDF_DIR%\sdk\x86\mupdf_wrapper.lib
echo.

endlocal
