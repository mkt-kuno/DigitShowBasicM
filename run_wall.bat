@echo off
cd /d "C:\Users\kmakoto\Desktop\DigitShowBasicM\src"
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" DigitShowBasicM.vcxproj /p:Configuration=Release /p:Platform=x64 /nologo /p:WarningLevel=EnableAllWarnings /t:Rebuild > ..\wall_out.txt 2>&1
echo DONE >> ..\wall_out.txt
