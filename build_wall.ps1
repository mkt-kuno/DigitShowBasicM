cd "C:\Users\kmakoto\Desktop\DigitShowBasicM"
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "DigitShowBasicM.sln" /p:Configuration=Release /p:Platform=x64 /nologo /p:WarningLevel=EnableAllWarnings /t:Rebuild 2>&1 | Set-Content "wall_warnings_all.txt"
Write-Host "Build done"
