setlocal
pushd %~dp0
set ProjectDir=%~dp0\..
set ProjectName=NiVerDig
set BinDir=%~dp0\..\..\x64\Release
set main=%BinDir%\NiVerDig.exe

:: get the file version from the .exe ...
set v=
FOR /F "tokens=2 delims==" %%a in ('wmic datafile where name^="%main:\=\\%" get Version /value') do set "v=%%a"
:: remove the last number
:loop
set c=%v:~-1%
set v=%v:~0,-1%
if "%c%" NEQ "." goto :loop

>.\ProductVersion.wxi (
echo ^<Include^>
echo  ^<?define ProductVersion=%v%?^>
echo ^</Include^> 
)
set wxsFileName=Product.wxs
set wixobjFileName=Product.wixobj
set msiFileName=%ProjectName%%v%.msi
set wixext=-ext "%WIX%\bin\WixUtilExtension.dll" -ext "%WIX%\bin\WixDifxAppExtension.dll" -ext "%WIX%\bin\WixUIExtension.dll" 
set resDir=..\res
set resFiles=res_files
set hexDir=..\hex
set hexFiles=hex_files

md obj
md ..\msi
"%WIX%\bin\candle.exe" -arch x64 %wxsFileName%  -out obj\%wixobjFileName%
"%WIX%\bin\heat.exe" dir "%resDir%" -cg cgResFiles -gg -scom -sreg -sfrag -srd -dr resDir -var env.resDir -out "obj\%resFiles%.wxs"
"%WIX%\bin\candle.exe" -arch x64 obj\%resFiles%.wxs -out obj\%resFiles%.wixobj
"%WIX%\bin\heat.exe" dir "%hexDir%" -cg cgHexFiles -gg -scom -sreg -sfrag -srd -dr hexDir -var env.hexDir -out "obj\%hexFiles%.wxs"
"%WIX%\bin\candle.exe" -arch x64 obj\%hexFiles%.wxs -out obj\%hexFiles%.wixobj
"%WIX%\bin\light.exe" %wixext% obj\%wixobjFileName% obj\%resFiles%.wixobj obj\%hexFiles%.wixobj -out ..\msi\%msiFileName%
popd
endlocal
