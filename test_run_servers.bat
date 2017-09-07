echo Create root
set root_folder=%~d0%~p0

rmdir %root_folder%servers /s /q
rmdir %root_folder%clients /s /q
del /q %root_folder%vds.log.new

echo creating server
build\app\vds_background\Debug\vds_background.exe server root -p 123qwe -r %root_folder%servers\0

echo starting server
start build\app\vds_background\Debug\vds_background.exe server start -r %root_folder%servers\0

FOR /L %%i IN (1,1,4) DO build\app\vds_node\Debug\vds_node.exe node install -l root -p 123qwe -r D:\projects\vds\servers\%%i

FOR /L %%i IN (1,1,4) DO start build\app\vds_background\Debug\vds_background.exe server start -r %root_folder%servers\%%i -P 805%%i

echo upload file
build\app\vds_node\Debug\vds_node.exe file upload -l root -p 123qwe -f %root_folder%vds.log -r %root_folder%clients\0

echo download file
build\app\vds_node\Debug\vds_node.exe file download -l root -p 123qwe -f %root_folder%vds.log.new -n vds.log -r %root_folder%clients\0

fc /b %root_folder%vds.log %root_folder%vds.log.new

rem https://localhost:8050/vds/dump_state
