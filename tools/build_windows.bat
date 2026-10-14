@echo off
rem Builds the VST3 and the standalone on Windows, then the installer if Inno Setup is present.
rem Needs: Visual Studio 2022 (Desktop development with C++), CMake, Git. JUCE is fetched.
cd /d "%~dp0\.."
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || exit /b 1
cmake --build build --config Release --target Textura_VST3 Textura_Standalone || exit /b 1
where iscc >nul 2>nul && iscc installer\textura.iss
echo.
echo VST3:       build\Textura_artefacts\Release\VST3\Textura.vst3
echo Standalone: build\Textura_artefacts\Release\Standalone\Textura.exe
echo To install by hand, copy Textura.vst3 into C:\Program Files\Common Files\VST3
