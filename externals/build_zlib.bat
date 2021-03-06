set root_folder=%~d0%~p0
rmdir %root_folder%build_zlib /s /q

rem call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
call ..\setenv.bat

mkdir %root_folder%build_zlib
cd %root_folder%build_zlib

rem cmake -DCMAKE_INSTALL_PREFIX=%root_folder%zlib_out\ -G "Visual Studio 15 2017 Win64" %root_folder%zlib\
cmake -DCMAKE_INSTALL_PREFIX=%root_folder%zlib_out\ -G "Visual Studio 14 2015 Win64" %root_folder%zlib\

cmake --build .
cmake --build . --target install

